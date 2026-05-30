/**
 * @file    shell/cmd_help.c
 * @brief   `help` — list registered commands with their one-line usage.
 */
#include <shell/shell.h>
#include <drivers/vga.h>

#define COL_USAGE  20

static void run(const uint8_t* args) {
    (void)args;
    uint32_t n = shell_cmd_count();
    for (uint32_t i = 0; i < n; i++) {
        const shell_cmd_t* c = shell_cmd_at(i);
        shell_indent();
        vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
        vga_write(c->name);
        /* pad to a common column for the description */
        while (vga_col() < SHELL_INDENT + COL_USAGE) vga_put_char(' ');
        vga_set_color(VGA_DARK_GREY, VGA_BLACK);
        vga_write(c->usage);
        vga_put_char('\n');
    }
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

const shell_cmd_t cmd_help = {
    .name  = (const uint8_t*)"help",
    .usage = (const uint8_t*)"list available commands",
    .run   = run,
};
