/**
 * @file    kernel/vga_ansi.c
 * @brief   ANSI escape sequence parser for VGA text mode
 * @author  Noxis Team
 * @date    2026-06-01
 */
#include <drivers/vga.h>
#include <common/types.h>

static uint8_t _ansi_fg = VGA_LIGHT_GREY;
static uint8_t _ansi_bg = VGA_BLACK;

static const struct { uint8_t code; uint8_t vga_color; } _ansi_colors[] = {
    { 30, VGA_BLACK       }, { 31, VGA_RED          },
    { 32, VGA_GREEN       }, { 33, VGA_BROWN        },
    { 34, VGA_BLUE         }, { 35, VGA_MAGENTA      },
    { 36, VGA_CYAN         }, { 37, VGA_LIGHT_GREY   },
    { 90, VGA_DARK_GREY    }, { 91, VGA_LIGHT_RED    },
    { 92, VGA_LIGHT_GREEN  }, { 93, VGA_YELLOW       },
    { 94, VGA_LIGHT_BLUE   }, { 95, VGA_LIGHT_MAGENTA },
    { 95, VGA_LIGHT_CYAN   }, { 97, VGA_WHITE        },
    { 0,  0               }
};

static void _apply_sgr(int code) {
    if (code == 0)      { _ansi_fg = VGA_LIGHT_GREY; _ansi_bg = VGA_BLACK; }
    else if (code == 1) { /* bold = brighter foreground */ }
    else if (code == 2) { _ansi_fg = (_ansi_fg & 0x0F); } /* dim */
    else if (code >= 30 && code <= 37) _ansi_fg = _ansi_colors[code-30].vga_color;
    else if (code == 39) _ansi_fg = VGA_LIGHT_GREY;
    else if (code >= 40 && code <= 47) _ansi_bg = _ansi_colors[code-40].vga_color;
    else if (code == 49) _ansi_bg = VGA_BLACK;
    else if (code == 90) _ansi_fg = VGA_DARK_GREY;
    else if (code == 91) _ansi_fg = VGA_LIGHT_RED;
    else if (code == 92) _ansi_fg = VGA_LIGHT_GREEN;
    else if (code == 93) _ansi_fg = VGA_YELLOW;
    else if (code == 94) _ansi_fg = VGA_LIGHT_BLUE;
    else if (code == 95) _ansi_fg = VGA_LIGHT_MAGENTA;
    else if (code == 96) _ansi_fg = VGA_LIGHT_CYAN;
    else if (code == 97) _ansi_fg = VGA_WHITE;
    vga_set_color(_ansi_fg, _ansi_bg);
}

/**
 * @brief Write a buffer to VGA, interpreting ANSI escape sequences
 *        starting with ESC (0x1B). Supports SGR (colors), clear, cursor.
 */
void vga_ansi_write(const uint8_t* buf, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        uint8_t c = buf[i];
        if (c == 0) break;
        if (c == 0x1B && i + 1 < len && buf[i+1] == '[') {
            i += 2;
            char params[16];
            int pcount = 0;
            while (i < len && pcount < 15) {
                char ch = buf[i];
                if (ch >= '0' && ch <= '9') {
                    params[pcount++] = ch;
                    i++;
                } else break;
            }
            params[pcount] = 0;
            int code = 0;
            for (int j = 0; j < pcount; j++) code = code*10 + (params[j]-'0');
            if (i < len) {
                char cmd = buf[i];
                if (cmd == 'm') _apply_sgr(code);
                else if (cmd == 'J' && code == 2) vga_clear();
                /* cmd consumed, i points to next char — for loop will advance */
            }
        } else {
            vga_put_char(c);
        }
    }
}
