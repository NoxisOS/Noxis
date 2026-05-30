/**
 * @file    kernel/early.c
 * @brief   Early kernel initialization
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
#include <proc/process.h>
#include <proc/scheduler.h>
#include <syscall/syscall.h>
#include <fs/vfs.h>

#define VGA_WIDTH    80
#define VGA_HEIGHT   25
#define VGA_BUFFER   ((volatile uint16_t*)0xB8000)
#define VGA_COLOR(fg, bg)  ((uint8_t)(((bg) << 4) | ((fg) & 0x0F)))

#define VGA_BLACK         0x0
#define VGA_BLUE          0x1
#define VGA_GREEN         0x2
#define VGA_CYAN          0x3
#define VGA_RED           0x4
#define VGA_LIGHT_GREY    0x7
#define VGA_DARK_GREY     0x8
#define VGA_LIGHT_GREEN   0xA
#define VGA_LIGHT_CYAN    0xB
#define VGA_LIGHT_RED     0xC
#define VGA_YELLOW        0xE
#define VGA_WHITE         0xF

static uint32_t g_row, g_col;
static uint8_t  g_color;

/* ── VGA primitives ─────────────────────────────────────────── */

static void _set_color(uint8_t fg, uint8_t bg) {
    g_color = VGA_COLOR(fg, bg);
}

/* Move the blinking hw cursor to (g_row, g_col) via CRTC ports. */
static void _vga_update_cursor(void) {
    uint16_t pos = (uint16_t)(g_row * VGA_WIDTH + g_col);
    port_byte_out(0x3D4, 0x0F);
    port_byte_out(0x3D5, (uint8_t)(pos & 0xFF));
    port_byte_out(0x3D4, 0x0E);
    port_byte_out(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void _vga_clear(void) {
    uint16_t b = (uint16_t)' ' | ((uint16_t)g_color << 8);
    for (uint32_t i = 0; i < VGA_WIDTH*VGA_HEIGHT; i++) VGA_BUFFER[i]=b;
    g_row=g_col=0;
}
static void _vga_scroll(void) {
    for (uint32_t i = 0; i < VGA_WIDTH*(VGA_HEIGHT-1); i++)
        VGA_BUFFER[i]=VGA_BUFFER[i+VGA_WIDTH];
    uint16_t b = (uint16_t)' ' | ((uint16_t)g_color << 8);
    for (uint32_t i = 0; i < VGA_WIDTH; i++)
        VGA_BUFFER[VGA_WIDTH*(VGA_HEIGHT-1)+i]=b;
}
static void _vga_put_char(uint8_t c) {
    if (c=='\n'){g_col=0;g_row++;}
    else if(c=='\r'){g_col=0;}
    else{VGA_BUFFER[g_row*VGA_WIDTH+g_col]=(uint16_t)c|((uint16_t)g_color<<8);g_col++;if(g_col>=VGA_WIDTH){g_col=0;g_row++;}}
    if(g_row>=VGA_HEIGHT){_vga_scroll();g_row=VGA_HEIGHT-1;}
    _vga_update_cursor();
}
static void _vga_write(const uint8_t* s) {
    for(uint32_t i=0;s[i];i++)_vga_put_char(s[i]);
}
static void _pad_to(uint32_t col, uint8_t fill) {
    while (g_col < col) _vga_put_char(fill);
}
static uint32_t _strlen(const uint8_t* s) {
    uint32_t n = 0; while (s[n]) n++; return n;
}

/* ── Banner (CP437 box-drawing) ─────────────────────────────── */
/*   ╔ 0xC9   ═ 0xCD   ╗ 0xBB   ║ 0xBA   ╚ 0xC8   ╝ 0xBC          */
/*   ► 0x10                                                       */

#define BANNER_INDENT  3
#define BANNER_INNER   68

static void _banner_edge(uint8_t l, uint8_t fill, uint8_t r) {
    _pad_to(BANNER_INDENT, ' ');
    _vga_put_char(l);
    for (uint32_t i = 0; i < BANNER_INNER; i++) _vga_put_char(fill);
    _vga_put_char(r);
    _vga_put_char('\n');
}

static void _banner_line(const uint8_t* content, uint8_t fg) {
    _set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    _pad_to(BANNER_INDENT, ' ');
    _vga_put_char(0xBA);
    _set_color(fg, VGA_BLACK);
    uint32_t len = _strlen(content);
    uint32_t pad_l = (BANNER_INNER - len) / 2;
    for (uint32_t i = 0; i < pad_l; i++) _vga_put_char(' ');
    _vga_write(content);
    _pad_to(BANNER_INDENT + 1 + BANNER_INNER, ' ');
    _set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    _vga_put_char(0xBA);
    _vga_put_char('\n');
}

static void _banner(void) {
    _vga_put_char('\n');
    _set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    _banner_edge(0xC9, 0xCD, 0xBB);
    _banner_line((const uint8_t*)"", VGA_WHITE);
    _banner_line((const uint8_t*)"N O X I S   O S       v 0 . 6 . 0", VGA_WHITE);
    _banner_line((const uint8_t*)"a tiny x86 kernel", VGA_DARK_GREY);
    _banner_line((const uint8_t*)"", VGA_WHITE);
    _set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    _banner_edge(0xC8, 0xCD, 0xBC);
    _set_color(VGA_LIGHT_GREY, VGA_BLACK);
    _vga_put_char('\n');
}

/* ── Step rows ──────────────────────────────────────────────── */

#define COL_CAT    6      /* "HAL   "                       */
#define COL_NAME   14     /* after "► "                     */
#define COL_DOTS_END 65   /* dots fill until here           */

static void _step_begin(const uint8_t* cat, const uint8_t* name) {
    _pad_to(COL_CAT, ' ');
    _set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    _vga_write(cat);
    _pad_to(COL_NAME - 2, ' ');
    _set_color(VGA_DARK_GREY, VGA_BLACK);
    _vga_put_char(0x10);                       /* ► */
    _vga_put_char(' ');
    _set_color(VGA_WHITE, VGA_BLACK);
    _vga_write(name);
    _vga_put_char(' ');
    _set_color(VGA_DARK_GREY, VGA_BLACK);
    _pad_to(COL_DOTS_END, '.');
    _vga_put_char(' ');
}

static void _step_ok(void) {
    _set_color(VGA_DARK_GREY, VGA_BLACK);
    _vga_put_char('[');
    _set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    _vga_write((const uint8_t*)" OK ");
    _set_color(VGA_DARK_GREY, VGA_BLACK);
    _vga_put_char(']');
    _set_color(VGA_LIGHT_GREY, VGA_BLACK);
    _vga_put_char('\n');
}

#define STEP(cat, name, action) do { \
    _step_begin((const uint8_t*)(cat), (const uint8_t*)(name)); \
    action; \
    _step_ok(); \
} while (0)

/* ── Footer ─────────────────────────────────────────────────── */

static void _footer(void) {
    _vga_put_char('\n');
    _pad_to(BANNER_INDENT, ' ');
    _set_color(VGA_DARK_GREY, VGA_BLACK);
    for (uint32_t i = 0; i < BANNER_INNER + 2; i++) _vga_put_char(0xC4); /* ─ */
    _vga_put_char('\n');
    _pad_to(COL_CAT, ' ');
    _set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    _vga_put_char(0x07);                          /* • bullet */
    _vga_put_char(' ');
    _set_color(VGA_WHITE, VGA_BLACK);
    _vga_write((const uint8_t*)"System ready ");
    _set_color(VGA_DARK_GREY, VGA_BLACK);
    _vga_write((const uint8_t*)"\xFA type 'help'");  /* · */
    _set_color(VGA_LIGHT_GREY, VGA_BLACK);
    _vga_put_char('\n');
}

/* ── Tiny REPL ──────────────────────────────────────────────── */

static void _prompt(void) {
    _set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    _vga_write((const uint8_t*)"   noxis");
    _set_color(VGA_DARK_GREY, VGA_BLACK);
    _vga_write((const uint8_t*)" > ");
    _set_color(VGA_WHITE, VGA_BLACK);
}

static void _backspace_visual(void) {
    if (g_col > 0) {
        g_col--;
        VGA_BUFFER[g_row * VGA_WIDTH + g_col] =
            (uint16_t)' ' | ((uint16_t)g_color << 8);
    }
}

static int _streq(const uint8_t* a, const uint8_t* b) {
    uint32_t i = 0;
    while (a[i] && b[i]) { if (a[i] != b[i]) return 0; i++; }
    return a[i] == b[i];
}

static void _cmd_help(void) {
    _set_color(VGA_DARK_GREY, VGA_BLACK);
    _vga_write((const uint8_t*)"   commands: ");
    _set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    _vga_write((const uint8_t*)"help  uptime  ls  cat <file>  clear  halt\n");
}

/* Print a uint32 padded right to `width` columns, in `fg` color */
static void _print_u32(uint32_t v, uint32_t width, uint8_t fg) {
    uint8_t buf[12]; uint32_t i = 11; buf[11] = 0;
    if (v == 0) { buf[--i] = '0'; }
    else { while (v) { buf[--i] = (uint8_t)('0' + (v % 10)); v /= 10; } }
    uint32_t len = 11 - i;
    _set_color(VGA_DARK_GREY, VGA_BLACK);
    while (len < width) { _vga_put_char(' '); len++; }
    _set_color(fg, VGA_BLACK);
    _vga_write(&buf[i]);
}

static void _cmd_ls(void) {
    uint32_t n = vfs_count();
    for (uint32_t i = 0; i < n; i++) {
        const vfs_file_t* f = vfs_entry(i);
        _set_color(VGA_DARK_GREY, VGA_BLACK);
        _vga_write((const uint8_t*)"   ");
        _print_u32(f->size, 6, VGA_YELLOW);
        _vga_write((const uint8_t*)"  ");
        _set_color(VGA_LIGHT_CYAN, VGA_BLACK);
        _vga_write(f->name);
        _vga_put_char('\n');
    }
}

static void _cmd_cat(const uint8_t* name) {
    const vfs_file_t* f = vfs_lookup(name);
    if (!f) {
        _set_color(VGA_LIGHT_RED, VGA_BLACK);
        _vga_write((const uint8_t*)"   no such file: ");
        _set_color(VGA_WHITE, VGA_BLACK);
        _vga_write(name);
        _vga_put_char('\n');
        return;
    }
    _set_color(VGA_LIGHT_GREY, VGA_BLACK);
    /* indent each line a bit to match the prompt */
    _vga_write((const uint8_t*)"   ");
    for (uint32_t i = 0; i < f->size; i++) {
        uint8_t c = f->data[i];
        _vga_put_char(c);
        if (c == '\n' && i + 1 < f->size) _vga_write((const uint8_t*)"   ");
    }
    if (f->size == 0 || f->data[f->size - 1] != '\n') _vga_put_char('\n');
}

static void _cmd_uptime(void) {
    uint32_t ms = pit_uptime_ms();
    uint32_t s  = ms / 1000;
    /* decimal print */
    uint8_t buf[12]; uint32_t i = 11; buf[11] = 0;
    if (s == 0) { buf[--i] = '0'; }
    else { while (s) { buf[--i] = (uint8_t)('0' + (s % 10)); s /= 10; } }
    _set_color(VGA_DARK_GREY, VGA_BLACK);
    _vga_write((const uint8_t*)"   ");
    _set_color(VGA_YELLOW, VGA_BLACK);
    _vga_write(&buf[i]);
    _set_color(VGA_DARK_GREY, VGA_BLACK);
    _vga_write((const uint8_t*)" seconds since boot\n");
}

static void _cmd_unknown(const uint8_t* line) {
    _set_color(VGA_LIGHT_RED, VGA_BLACK);
    _vga_write((const uint8_t*)"   unknown: ");
    _set_color(VGA_WHITE, VGA_BLACK);
    _vga_write(line);
    _vga_put_char('\n');
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

static void _run_command(const uint8_t* line) {
    const uint8_t* arg;
    if (line[0] == 0) return;
    if      (_streq(line, (const uint8_t*)"help"))   _cmd_help();
    else if (_streq(line, (const uint8_t*)"uptime")) _cmd_uptime();
    else if (_streq(line, (const uint8_t*)"ls"))     _cmd_ls();
    else if (_streq(line, (const uint8_t*)"clear"))  { _vga_clear(); }
    else if (_streq(line, (const uint8_t*)"halt"))   { for (;;) cpu_hlt(); }
    else if ((arg = _match_prefix(line, (const uint8_t*)"cat")) != 0) {
        if (arg[0] == 0) {
            _set_color(VGA_LIGHT_RED, VGA_BLACK);
            _vga_write((const uint8_t*)"   usage: cat <file>\n");
        } else {
            _cmd_cat(arg);
        }
    }
    else                                              _cmd_unknown(line);
}

static void _repl(void) {
    uint8_t line[80];
    uint32_t len = 0;

    _prompt();
    for (;;) {
        uint8_t c = kbd_getchar();
        if (c == '\n') {
            _vga_put_char('\n');
            line[len] = 0;
            _run_command(line);
            len = 0;
            _prompt();
        } else if (c == '\b') {
            if (len > 0) { len--; _backspace_visual(); }
        } else if (c >= ' ' && c < 0x7F) {
            if (len < sizeof(line) - 1) {
                line[len++] = c;
                _set_color(VGA_WHITE, VGA_BLACK);
                _vga_put_char(c);
            }
        }
    }
}

/* ── Entry ──────────────────────────────────────────────────── */

void kernel_main(void) {
    _set_color(VGA_LIGHT_GREY, VGA_BLACK);
    _vga_clear();

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

    cpu_sti();    /* make sure IRQs are on for kbd + pit */
    _vga_put_char('\n');
    _repl();
}
