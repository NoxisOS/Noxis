/**
 * @file    kernel/early.c
 * @brief   Early kernel initialization — VGA output, main entry
 * @author  Noxis Team
 * @date    2026-05-29
 */
#include <common/types.h>

/* ── VGA constants ─────────────────────────────────────────── */
#define VGA_WIDTH    80
#define VGA_HEIGHT   25
#define VGA_BUFFER   ((volatile uint16_t*)0xB8000)
#define VGA_COLOR(fg, bg)  ((uint8_t)(((bg) << 4) | ((fg) & 0x0F)))

/* ── VGA colors ────────────────────────────────────────────── */
#define VGA_BLACK         0x0
#define VGA_GREEN         0x2
#define VGA_RED           0x4
#define VGA_LIGHT_GREY    0x7
#define VGA_WHITE         0xF

/* ── file-scope state ──────────────────────────────────────── */
static uint32_t g_row;
static uint32_t g_col;
static uint8_t  g_color;

/* ── private functions ─────────────────────────────────────── */
static void _vga_clear(void) {
    volatile uint16_t* buf = VGA_BUFFER;
    uint16_t blank = (uint16_t)' ' | ((uint16_t)g_color << 8);
    for (uint32_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        buf[i] = blank;
    }
    g_row = 0;
    g_col = 0;
}

static void _vga_scroll(void) {
    volatile uint16_t* buf = VGA_BUFFER;
    /* Move lines 1-24 up by one row */
    for (uint32_t i = 0; i < VGA_WIDTH * (VGA_HEIGHT - 1); i++) {
        buf[i] = buf[i + VGA_WIDTH];
    }
    /* Clear last line */
    uint16_t blank = (uint16_t)' ' | ((uint16_t)g_color << 8);
    uint32_t last = VGA_WIDTH * (VGA_HEIGHT - 1);
    for (uint32_t i = 0; i < VGA_WIDTH; i++) {
        buf[last + i] = blank;
    }
}

static void _vga_put_char(uint8_t c) {
    if (c == '\n') {
        g_col = 0;
        g_row++;
    } else if (c == '\r') {
        g_col = 0;
    } else if (c == '\t') {
        g_col = (g_col + 4) & ~3;
        if (g_col >= VGA_WIDTH) {
            g_col = 0;
            g_row++;
        }
    } else {
        uint32_t idx = g_row * VGA_WIDTH + g_col;
        VGA_BUFFER[idx] = (uint16_t)c | ((uint16_t)g_color << 8);
        g_col++;
        if (g_col >= VGA_WIDTH) {
            g_col = 0;
            g_row++;
        }
    }
    if (g_row >= VGA_HEIGHT) {
        _vga_scroll();
        g_row = VGA_HEIGHT - 1;
    }
}

static void _vga_write(const uint8_t* str) {
    for (uint32_t i = 0; str[i] != '\0'; i++) {
        _vga_put_char(str[i]);
    }
}

/* ── public functions ──────────────────────────────────────── */

/**
 * @brief Kernel main entry point — called from ASM trampoline
 */
void kernel_main(void) {
    /* Initialize VGA with green-on-black */
    g_color = VGA_COLOR(VGA_LIGHT_GREY, VGA_BLACK);
    _vga_clear();

    _vga_write((const uint8_t*)"\n");
    _vga_write((const uint8_t*)"   Noxis OS v0.1.0\n");
    _vga_write((const uint8_t*)"   ===============\n");
    _vga_write((const uint8_t*)"\n");
    _vga_write((const uint8_t*)"   Booted successfully in 32-bit protected mode.\n");
    _vga_write((const uint8_t*)"   Kernel loaded at 0x100000.\n");
    _vga_write((const uint8_t*)"\n");
    _vga_write((const uint8_t*)"   System halted.\n");

    /* Halt forever */
    for (;;);
}
