/**
 * @file    drivers/vga.c
 * @brief   VGA text-mode driver — single source of truth for cursor + color.
 * @author  Noxis Team
 * @date    2026-05-30
 */
#include <drivers/vga.h>
#include <kernel/hal/ports.h>
#include <common/types.h>

#define VGA_BUFFER  ((volatile uint16_t*)0xB8000)
#define VGA_COLOR(fg, bg)  ((uint8_t)(((bg) << 4) | ((fg) & 0x0F)))

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
    else {
        VGA_BUFFER[g_row * VGA_WIDTH + g_col] = (uint16_t)c | ((uint16_t)g_color << 8);
        g_col++;
        if (g_col >= VGA_WIDTH) { g_col = 0; g_row++; }
    }
    if (g_row >= VGA_HEIGHT) { _scroll(); g_row = VGA_HEIGHT - 1; }
}

/* ── Minimal ANSI terminal: SGR colour + clear/home/cursor ───────
   Supported:
     ESC[0m            reset to light-grey on black
     ESC[<n>m          SGR: 30-37/90-97 fg, 40-47/100-107 bg, 1 = bright
     ESC[2J            clear screen
     ESC[H  / ESC[r;cH cursor home / move (1-based)
   Anything unrecognised is silently dropped (no garbage on screen). */

/* Map ANSI colour index (0-7) to VGA, with a "bright" flag. */
static uint8_t _ansi_to_vga(uint32_t idx, int bright) {
    static const uint8_t base[8]   = {
        VGA_BLACK, VGA_RED, VGA_GREEN, VGA_BROWN,
        VGA_BLUE, VGA_MAGENTA, VGA_CYAN, VGA_LIGHT_GREY
    };
    static const uint8_t bright_[8] = {
        VGA_DARK_GREY, VGA_LIGHT_RED, VGA_LIGHT_GREEN, VGA_YELLOW,
        VGA_LIGHT_BLUE, VGA_LIGHT_MAGENTA, VGA_LIGHT_CYAN, VGA_WHITE
    };
    if (idx > 7) idx = 7;
    return bright ? bright_[idx] : base[idx];
}

/* Escape-sequence parser state. */
static int      g_esc;            /* 0 normal, 1 saw ESC, 2 in CSI */
static uint32_t g_params[4];
static uint32_t g_nparam;
static int      g_bright;

static void _apply_sgr(void) {
    uint8_t fg = g_color & 0x0F;
    uint8_t bg = (g_color >> 4) & 0x0F;
    if (g_nparam == 0) { fg = VGA_LIGHT_GREY; bg = VGA_BLACK; g_bright = 0; }
    for (uint32_t i = 0; i < g_nparam; i++) {
        uint32_t p = g_params[i];
        if (p == 0)        { fg = VGA_LIGHT_GREY; bg = VGA_BLACK; g_bright = 0; }
        else if (p == 1)   g_bright = 1;
        else if (p >= 30 && p <= 37)   fg = _ansi_to_vga(p - 30, g_bright);
        else if (p >= 90 && p <= 97)   fg = _ansi_to_vga(p - 90, 1);
        else if (p >= 40 && p <= 47)   bg = _ansi_to_vga(p - 40, 0);
        else if (p >= 100 && p <= 107) bg = _ansi_to_vga(p - 100, 1);
    }
    g_color = VGA_COLOR(fg, bg);
}

static void _csi_final(uint8_t c) {
    switch (c) {
    case 'm': _apply_sgr(); break;
    case 'J': if (g_nparam && g_params[0] == 2) vga_clear(); break;
    case 'H': {
        uint32_t r = g_nparam > 0 && g_params[0] ? g_params[0] - 1 : 0;
        uint32_t cc = g_nparam > 1 && g_params[1] ? g_params[1] - 1 : 0;
        if (r >= VGA_HEIGHT) r = VGA_HEIGHT - 1;
        if (cc >= VGA_WIDTH) cc = VGA_WIDTH - 1;
        g_row = r; g_col = cc;
        break;
    }
    default: break;
    }
}

void vga_put_char(uint8_t c) {
    __asm__ __volatile__("cli");

    if (g_esc == 0) {
        if (c == 0x1B) { g_esc = 1; }        /* ESC */
        else            _raw_putc(c);
    } else if (g_esc == 1) {
        if (c == '[') { g_esc = 2; g_nparam = 0; g_params[0] = 0; g_bright = 0; }
        else          { g_esc = 0; }          /* unsupported / drop */
    } else { /* g_esc == 2: collecting CSI */
        if (c >= '0' && c <= '9') {
            if (g_nparam == 0) g_nparam = 1;
            g_params[g_nparam - 1] = g_params[g_nparam - 1] * 10 + (c - '0');
        } else if (c == ';') {
            if (g_nparam < 4) { g_params[g_nparam] = 0; g_nparam++; }
        } else {
            _csi_final(c);
            g_esc = 0;
        }
    }

    vga_update_cursor();
    __asm__ __volatile__("sti");
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
