/**
 * @file    drivers/tty/tty.c
 * @brief   TTY subsystem — canonical/raw mode, echo, line editing, signals.
 *
 * Data flow:
 *   Keyboard ISR → tty_input(c) → line buffer (canonical) or read queue (raw)
 *   User process  → tty_read()   → copies from read queue to user buf
 *
 * @author  Noxis Team
 * @date    2026-05-30
 */
#include <drivers/tty/tty.h>
#include <drivers/vga.h>
#include <proc/scheduler.h>
#include <proc/process.h>
#include <common/signal.h>

/* ── file-scope state ──────────────────────────────────────── */

static uint8_t  g_readq[TTY_BUF_SIZE];
static uint32_t g_readq_head;
static uint32_t g_readq_tail;
static uint32_t g_readq_count;

static uint8_t  g_line[TTY_BUF_SIZE];
static uint32_t g_line_len;

static termios_t g_termios = { .lflag = ICANON | ECHO | ISIG };
static process_t* g_reader;

/* ── internal helpers ──────────────────────────────────────── */

static void _readq_push(uint8_t c) {
    if (g_readq_count >= TTY_BUF_SIZE) return;
    g_readq[g_readq_head] = c;
    g_readq_head = (g_readq_head + 1) % TTY_BUF_SIZE;
    g_readq_count++;
}

static int32_t _readq_pop(void) {
    if (g_readq_count == 0) return -1;
    uint8_t c = g_readq[g_readq_tail];
    g_readq_tail = (g_readq_tail + 1) % TTY_BUF_SIZE;
    g_readq_count--;
    return (int32_t)c;
}

static void _readq_commit_line(void) {
    for (uint32_t i = 0; i < g_line_len; i++)
        _readq_push(g_line[i]);
    _readq_push('\n');
    g_line_len = 0;
}

static void _echo(uint8_t c) {
    if (!(g_termios.lflag & ECHO)) return;
    if (c == '\n') { vga_put_char('\n'); return; }
    if (c >= ' ' && c < 0x7F) vga_put_char(c);
}

static void _echo_bs(void) {
    if (!(g_termios.lflag & ECHO)) return;
    vga_backspace();
}

/* ── public API ────────────────────────────────────────────── */

void tty_input(uint8_t c) {
    if (!(g_termios.lflag & ICANON)) {
        _readq_push(c);
        _echo(c);
        scheduler_wake(&g_reader);
        return;
    }

    /* ── canonical mode ───────────────────────────────────── */

    /* Ctrl+C → SIGINT */
    if ((g_termios.lflag & ISIG) && c == 0x03) {
        process_t* fg = g_reader;
        if (fg && fg->page_dir_phys) {
            fg->sig_pending |= (1u << SIGINT);
        }
        g_line_len = 0;
        _echo('^'); _echo('C'); vga_put_char('\n');
        scheduler_wake(&g_reader);
        return;
    }

    /* Ctrl+D → EOF (only at start of line) */
    if (c == 0x04) {
        if (g_line_len == 0) {
            _readq_push(0);
            scheduler_wake(&g_reader);
        }
        return;
    }

    /* Backspace */
    if (c == '\b' || c == 0x7F) {
        if (g_line_len > 0) {
            g_line_len--;
            _echo_bs();
        }
        return;
    }

    /* Enter */
    if (c == '\n' || c == '\r') {
        _readq_commit_line();
        _echo('\n');
        scheduler_wake(&g_reader);
        return;
    }

    /* Filter non-printable (except tab) */
    if (c != '\t' && (c < ' ' || c >= 0x7F)) return;

    /* Normal character */
    if (g_line_len < TTY_BUF_SIZE - 1) {
        g_line[g_line_len++] = c;
        _echo(c);
    }
}

int32_t tty_read(uint8_t* buf, uint32_t len) {
    if (!buf || len == 0) return 0;

    while (g_readq_count == 0) {
        g_reader = scheduler_current();
        scheduler_block_on(&g_reader);

        process_t* cur = scheduler_current();
        if (cur && (cur->sig_pending & ~cur->sig_blocked))
            return -1;
    }

    uint32_t copied = 0;
    __asm__ __volatile__("cli");
    while (copied < len && g_readq_count > 0) {
        int32_t c = _readq_pop();
        if (c < 0) break;
        if (c == 0) {
            __asm__ __volatile__("sti");
            return (int32_t)copied;
        }
        buf[copied++] = (uint8_t)c;
    }
    __asm__ __volatile__("sti");
    return (int32_t)copied;
}

int32_t tty_ioctl(uint32_t req, void* arg) {
    if (!arg) return -1;

    switch (req) {
    case TCGETS: {
        termios_t* tp = (termios_t*)arg;
        tp->lflag = g_termios.lflag;
        return 0;
    }
    case TCSETS: {
        termios_t* tp = (termios_t*)arg;
        g_termios.lflag = tp->lflag;
        return 0;
    }
    default:
        return -1;
    }
}

os_status_t tty_init(void) {
    g_readq_head  = 0;
    g_readq_tail  = 0;
    g_readq_count = 0;
    g_line_len    = 0;
    g_reader      = (process_t*)0;
    g_termios.lflag = ICANON | ECHO | ISIG;
    return OS_OK;
}
