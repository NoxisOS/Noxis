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
#include <proc/process.h>
#include <proc/scheduler.h>
#include <syscall/syscall.h>

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
    _vga_write((const uint8_t*)"\xFA CPU halted");  /* · */
    _set_color(VGA_LIGHT_GREY, VGA_BLACK);
    _vga_put_char('\n');
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
    STEP("DRV",  "ATA",     ata_init(ATA_PRIMARY, ATA_MASTER));
    STEP("PROC", "SCHED",   scheduler_init());
    STEP("SYS",  "SYSCALL", syscall_init());

    _footer();

    for (;;);
}
