/**
 * @file    drivers/vga.c
 * @brief   VGA text-mode driver — single source of truth for cursor + color.
 * @author  Noxis Team
 * @date    2026-05-30
 */
#include <drivers/vga.h>
#include <kernel/hal/ports.h>
#include <common/types.h>

#define VGA_COLOR(fg, bg)  ((uint8_t)(((bg) << 4) | ((fg) & 0x0F)))

/* The text buffer is reached at low phys 0xB8000 during early boot (valid via
   the boot identity map), then through the physmap once paging is live, so it
   stays accessible from every address space (the low half is per-process). */
#define VGA_LOW   ((volatile uint16_t*)0xB8000ULL)
#define VGA_HIGH  ((volatile uint16_t*)(0xFFFF800000000000ULL + 0xB8000))
static volatile uint16_t* VGA_BUFFER = VGA_LOW;

void vga_use_physmap(void) { VGA_BUFFER = VGA_HIGH; }

static uint32_t g_row;
static uint32_t g_col;
static uint8_t  g_color;

/* ── private ────────────────────────────────────────────────── */

static void _scroll(void) {
    for (uint32_t i = 0; i < VGA_WIDTH * (VGA_HEIGHT - 1); i++) {
        VGA_BUFFER[i] = VGA_BUFFER[i + VGA_WIDTH];
    }
    uint16_t blank = (uint16_t)' ' | ((uint16_t)g_color << 8);
    for (uint32_t i = 0; i < VGA_WIDTH; i++) {
        VGA_BUFFER[VGA_WIDTH * (VGA_HEIGHT - 1) + i] = blank;
    }
}

/* ── public ─────────────────────────────────────────────────── */

void vga_init(void) {
    g_row = 0;
    g_col = 0;
    g_color = VGA_COLOR(VGA_LIGHT_GREY, VGA_BLACK);
}

void vga_clear(void) {
    uint16_t blank = (uint16_t)' ' | ((uint16_t)g_color << 8);
    for (uint32_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) VGA_BUFFER[i] = blank;
    g_row = 0;
    g_col = 0;
    vga_update_cursor();
}

void vga_set_color(uint8_t fg, uint8_t bg) {
    g_color = VGA_COLOR(fg, bg);
}

uint8_t vga_color(void) { return g_color; }

/* ── Raw glyph output (no escape interpretation) ─────────────── */
static void _raw_putc(uint8_t c) {
    if (c == '\n') { g_col = 0; g_row++; }
    else if (c == '\r') { g_col = 0; }
    else if (c == '\b') { if (g_col > 0) g_col--; }
    else {
        VGA_BUFFER[g_row * VGA_WIDTH + g_col] = (uint16_t)c | ((uint16_t)g_color << 8);
        g_col++;
        if (g_col >= VGA_WIDTH) { g_col = 0; g_row++; }
    }
    if (g_row >= VGA_HEIGHT) { _scroll(); g_row = VGA_HEIGHT - 1; }
}

void vga_put_char(uint8_t c) {
    __asm__ __volatile__("cli");
    _raw_putc(c);
    vga_update_cursor();
    __asm__ __volatile__("sti");
}

void vga_write_buf(const uint8_t* buf, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) vga_put_char(buf[i]);
}

void vga_write(const uint8_t* s) {
    for (uint32_t i = 0; s[i]; i++) vga_put_char(s[i]);
}

void vga_pad_to(uint32_t col, uint8_t fill) {
    while (g_col < col && g_col < VGA_WIDTH) vga_put_char(fill);
}

void vga_backspace(void) {
    if (g_col > 0) {
        g_col--;
        VGA_BUFFER[g_row * VGA_WIDTH + g_col] =
            (uint16_t)' ' | ((uint16_t)g_color << 8);
        vga_update_cursor();
    }
}

uint32_t vga_row(void) { return g_row; }
uint32_t vga_col(void) { return g_col; }

void vga_prime_cursor(void) {
    VGA_BUFFER[g_row * VGA_WIDTH + g_col] =
        (uint16_t)' ' | ((uint16_t)g_color << 8);
}

void vga_update_cursor(void) {
    uint16_t pos = (uint16_t)(g_row * VGA_WIDTH + g_col);
    port_byte_out(0x3D4, 0x0F);
    port_byte_out(0x3D5, (uint8_t)(pos & 0xFF));
    port_byte_out(0x3D4, 0x0E);
    port_byte_out(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}
