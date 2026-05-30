/**
 * @file    drivers/vga.h
 * @brief   VGA text-mode driver — shared between kernel and syscall handlers.
 *          Owns g_row/g_col/g_color. All writers (banner, REPL, sys_write)
 *          go through this so the cursor never gets out of sync.
 * @author  Noxis Team
 * @date    2026-05-30
 */
#ifndef DRIVERS_VGA_H
#define DRIVERS_VGA_H

#include <common/types.h>

/* CP437 colors (low nibble = fg, high nibble = bg in attribute byte). */
#define VGA_BLACK         0x0
#define VGA_BLUE          0x1
#define VGA_GREEN         0x2
#define VGA_CYAN          0x3
#define VGA_RED           0x4
#define VGA_MAGENTA       0x5
#define VGA_BROWN         0x6
#define VGA_LIGHT_GREY    0x7
#define VGA_DARK_GREY     0x8
#define VGA_LIGHT_BLUE    0x9
#define VGA_LIGHT_GREEN   0xA
#define VGA_LIGHT_CYAN    0xB
#define VGA_LIGHT_RED     0xC
#define VGA_LIGHT_MAGENTA 0xD
#define VGA_YELLOW        0xE
#define VGA_WHITE         0xF

#define VGA_WIDTH    80
#define VGA_HEIGHT   25

/* Setup + screen ops */
void     vga_init(void);
void     vga_clear(void);

/* Color */
void     vga_set_color(uint8_t fg, uint8_t bg);
uint8_t  vga_color(void);

/* Writing */
void     vga_put_char(uint8_t c);
void     vga_write(const uint8_t* s);

/* Layout helpers */
void     vga_pad_to(uint32_t col, uint8_t fill);
void     vga_backspace(void);   /* visual: erase char left of cursor */

/* Cursor introspection */
uint32_t vga_row(void);
uint32_t vga_col(void);

/* Hardware cursor update (auto-called by put_char) */
void     vga_update_cursor(void);

#endif /* DRIVERS_VGA_H */
