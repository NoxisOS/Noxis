/**
 * @file    kernel/early.c
 * @brief   Boot banner, init sequence, handoff to the shell.
 *          Once VFS is up, this file does nothing — the shell owns the REPL
 *          and each builtin lives in shell/cmd_*.c.
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
#include <drivers/pit.h>
#include <drivers/ata.h>
#include <drivers/kbd.h>
#include <drivers/vga.h>
#include <proc/scheduler.h>
#include <syscall/syscall.h>
#include <fs/vfs.h>
#include <shell/shell.h>

/* ── small string helper (used by banner centering) ─────────── */

static uint32_t _strlen(const uint8_t* s) {
    uint32_t n = 0; while (s[n]) n++; return n;
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
    uint32_t pad_l = (BANNER_INNER - _strlen(content)) / 2;
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
    shell_run();
}
