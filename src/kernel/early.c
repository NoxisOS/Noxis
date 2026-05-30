/**
 * @file    kernel/early.c
 * @brief   Boot banner, init sequence, interactive shell.
 *          All screen output goes through drivers/vga so user-mode sys_write
 *          and kernel printing stay in sync on the same cursor.
 * @author  Noxis Team
 * @date    2026-05-30
 */
#include <common/types.h>
#include <hal/gdt.h>
#include <hal/idt.h>
#include <hal/pic.h>
#include <hal/ports.h>
#include <kernel/isr.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <mm/heap.h>
#include <mm/paging.h>
#include <drivers/pit.h>
#include <drivers/ata.h>
#include <drivers/kbd.h>
#include <drivers/vga.h>
#include <proc/process.h>
#include <proc/scheduler.h>
#include <proc/exec.h>
#include <syscall/syscall.h>
#include <fs/vfs.h>

/* ── small string helpers ───────────────────────────────────── */

static uint32_t _strlen(const uint8_t* s) {
    uint32_t n = 0; while (s[n]) n++; return n;
}

static int _streq(const uint8_t* a, const uint8_t* b) {
    uint32_t i = 0;
    while (a[i] && b[i]) { if (a[i] != b[i]) return 0; i++; }
    return a[i] == b[i];
}

/* If `line` starts with `prefix` followed by '\0' or spaces, return a pointer
   to the trimmed argument (may be empty string). Otherwise NULL. */
static const uint8_t* _match_prefix(const uint8_t* line, const uint8_t* prefix) {
    uint32_t i = 0;
    while (prefix[i]) { if (line[i] != prefix[i]) return (const uint8_t*)0; i++; }
    if (line[i] == 0) return line + i;
    if (line[i] != ' ') return (const uint8_t*)0;
    while (line[i] == ' ') i++;
    return line + i;
}

/* ── Banner (CP437 box-drawing) ─────────────────────────────── */
/*   ╔ 0xC9   ═ 0xCD   ╗ 0xBB   ║ 0xBA   ╚ 0xC8   ╝ 0xBC          */
/*   ► 0x10   ─ 0xC4   · 0xFA   • 0x07                            */

#define BANNER_INDENT  3
#define BANNER_INNER   68

static void _banner_edge(uint8_t l, uint8_t fill, uint8_t r) {
    vga_pad_to(BANNER_INDENT, ' ');
    vga_put_char(l);
    for (uint32_t i = 0; i < BANNER_INNER; i++) vga_put_char(fill);
    vga_put_char(r);
    vga_put_char('\n');
}

static void _banner_line(const uint8_t* content, uint8_t fg) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_pad_to(BANNER_INDENT, ' ');
    vga_put_char(0xBA);
    vga_set_color(fg, VGA_BLACK);
    uint32_t len = _strlen(content);
    uint32_t pad_l = (BANNER_INNER - len) / 2;
    for (uint32_t i = 0; i < pad_l; i++) vga_put_char(' ');
    vga_write(content);
    vga_pad_to(BANNER_INDENT + 1 + BANNER_INNER, ' ');
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_put_char(0xBA);
    vga_put_char('\n');
}

static void _banner(void) {
    vga_put_char('\n');
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    _banner_edge(0xC9, 0xCD, 0xBB);
    _banner_line((const uint8_t*)"", VGA_WHITE);
    _banner_line((const uint8_t*)"N O X I S   O S       v 0 . 7 . 0", VGA_WHITE);
    _banner_line((const uint8_t*)"a tiny x86 kernel", VGA_DARK_GREY);
    _banner_line((const uint8_t*)"", VGA_WHITE);
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    _banner_edge(0xC8, 0xCD, 0xBC);
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_put_char('\n');
}

/* ── Step rows ──────────────────────────────────────────────── */

#define COL_CAT      6
#define COL_NAME     14
#define COL_DOTS_END 65

static void _step_begin(const uint8_t* cat, const uint8_t* name) {
    vga_pad_to(COL_CAT, ' ');
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_write(cat);
    vga_pad_to(COL_NAME - 2, ' ');
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_put_char(0x10);
    vga_put_char(' ');
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_write(name);
    vga_put_char(' ');
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_pad_to(COL_DOTS_END, '.');
    vga_put_char(' ');
}

static void _step_ok(void) {
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_put_char('[');
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_write((const uint8_t*)" OK ");
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_put_char(']');
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_put_char('\n');
}

#define STEP(cat, name, action) do { \
    _step_begin((const uint8_t*)(cat), (const uint8_t*)(name)); \
    action; \
    _step_ok(); \
} while (0)

/* ── Footer ─────────────────────────────────────────────────── */

static void _footer(void) {
    vga_put_char('\n');
    vga_pad_to(BANNER_INDENT, ' ');
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    for (uint32_t i = 0; i < BANNER_INNER + 2; i++) vga_put_char(0xC4);
    vga_put_char('\n');
    vga_pad_to(COL_CAT, ' ');
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_put_char(0x07);
    vga_put_char(' ');
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_write((const uint8_t*)"System ready ");
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_write((const uint8_t*)"\xFA type 'help'");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_put_char('\n');
}

/* ── shell prompt ───────────────────────────────────────────── */

static void _prompt(void) {
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_write((const uint8_t*)"   noxis");
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_write((const uint8_t*)" > ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
}

/* ── commands ───────────────────────────────────────────────── */

static void _cmd_help(void) {
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_write((const uint8_t*)"   commands: ");
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_write((const uint8_t*)"help  uptime  ls  cat <f>  exec <f>  clear  halt\n");
}

static void _cmd_uptime(void) {
    uint32_t ms = pit_uptime_ms();
    uint32_t s  = ms / 1000;
    uint8_t buf[12]; uint32_t i = 11; buf[11] = 0;
    if (s == 0) { buf[--i] = '0'; }
    else { while (s) { buf[--i] = (uint8_t)('0' + (s % 10)); s /= 10; } }
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_write((const uint8_t*)"   ");
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_write(&buf[i]);
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_write((const uint8_t*)" seconds since boot\n");
}

static void _print_u32(uint32_t v, uint32_t width, uint8_t fg) {
    uint8_t buf[12]; uint32_t i = 11; buf[11] = 0;
    if (v == 0) { buf[--i] = '0'; }
    else { while (v) { buf[--i] = (uint8_t)('0' + (v % 10)); v /= 10; } }
    uint32_t len = 11 - i;
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    while (len < width) { vga_put_char(' '); len++; }
    vga_set_color(fg, VGA_BLACK);
    vga_write(&buf[i]);
}

static void _cmd_ls(void) {
    uint32_t n = vfs_count();
    for (uint32_t i = 0; i < n; i++) {
        const vfs_file_t* f = vfs_entry(i);
        vga_set_color(VGA_DARK_GREY, VGA_BLACK);
        vga_write((const uint8_t*)"   ");
        _print_u32(f->size, 6, VGA_YELLOW);
        vga_write((const uint8_t*)"  ");
        vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
        vga_write(f->name);
        vga_put_char('\n');
    }
}

static void _cmd_cat(const uint8_t* name) {
    const vfs_file_t* f = vfs_lookup(name);
    if (!f) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_write((const uint8_t*)"   no such file: ");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        vga_write(name);
        vga_put_char('\n');
        return;
    }
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_write((const uint8_t*)"   ");
    for (uint32_t i = 0; i < f->size; i++) {
        uint8_t c = f->data[i];
        vga_put_char(c);
        if (c == '\n' && i + 1 < f->size) vga_write((const uint8_t*)"   ");
    }
    if (f->size == 0 || f->data[f->size - 1] != '\n') vga_put_char('\n');
}

static void _cmd_exec(const uint8_t* name) {
    const vfs_file_t* f = vfs_lookup(name);
    if (!f) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_write((const uint8_t*)"   no such file: ");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        vga_write(name); vga_put_char('\n');
        return;
    }

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_write((const uint8_t*)"   exec ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_write(name);
    vga_put_char('\n');
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    int exit_code = 0;
    os_status_t s = exec_run(f->data, f->size, &exit_code);
    if (s != OS_OK) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_write((const uint8_t*)"   exec failed (not a valid ELF or OOM)\n");
        return;
    }

    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_write((const uint8_t*)"   [exited ");
    _print_u32((uint32_t)exit_code, 0, VGA_YELLOW);
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_write((const uint8_t*)"]\n");
}

static void _cmd_unknown(const uint8_t* line) {
    vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
    vga_write((const uint8_t*)"   unknown: ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_write(line);
    vga_put_char('\n');
}

static void _run_command(const uint8_t* line) {
    const uint8_t* arg;
    if (line[0] == 0) return;
    if      (_streq(line, (const uint8_t*)"help"))   _cmd_help();
    else if (_streq(line, (const uint8_t*)"uptime")) _cmd_uptime();
    else if (_streq(line, (const uint8_t*)"ls"))     _cmd_ls();
    else if (_streq(line, (const uint8_t*)"clear"))  vga_clear();
    else if (_streq(line, (const uint8_t*)"halt"))   { for (;;) cpu_hlt(); }
    else if ((arg = _match_prefix(line, (const uint8_t*)"cat")) != 0) {
        if (arg[0] == 0) {
            vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
            vga_write((const uint8_t*)"   usage: cat <file>\n");
        } else _cmd_cat(arg);
    }
    else if ((arg = _match_prefix(line, (const uint8_t*)"exec")) != 0) {
        if (arg[0] == 0) {
            vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
            vga_write((const uint8_t*)"   usage: exec <file>\n");
        } else _cmd_exec(arg);
    }
    else _cmd_unknown(line);
}

static void _repl(void) {
    uint8_t line[80];
    uint32_t len = 0;

    _prompt();
    for (;;) {
        uint8_t c = kbd_getchar();
        if (c == '\n') {
            vga_put_char('\n');
            line[len] = 0;
            _run_command(line);
            len = 0;
            _prompt();
        } else if (c == '\b') {
            if (len > 0) { len--; vga_backspace(); }
        } else if (c >= ' ' && c < 0x7F) {
            if (len < sizeof(line) - 1) {
                line[len++] = c;
                vga_set_color(VGA_WHITE, VGA_BLACK);
                vga_put_char(c);
            }
        }
    }
}

/* ── Entry ──────────────────────────────────────────────────── */

void kernel_main(void) {
    vga_init();
    vga_clear();

    _banner();

    STEP("HAL",  "GDT",     gdt_init());
    STEP("HAL",  "IDT",     idt_init());
    STEP("HAL",  "PIC",     pic_remap());
    STEP("KRN",  "ISR",     isr_init());
    STEP("MM",   "PMM",     pmm_init(128*1024*1024));
    STEP("MM",   "VMM",     (void)0);
    STEP("MM",   "HEAP",    heap_init());
    STEP("DRV",  "PIT",     pit_init(1000));
    STEP("DRV",  "KBD",     kbd_init());
    STEP("DRV",  "ATA",     ata_init(ATA_PRIMARY, ATA_MASTER));
    STEP("PROC", "SCHED",   scheduler_init());
    STEP("SYS",  "SYSCALL", syscall_init());
    STEP("FS",   "VFS",     vfs_init());

    _footer();

    cpu_sti();
    vga_put_char('\n');
    _repl();
}
