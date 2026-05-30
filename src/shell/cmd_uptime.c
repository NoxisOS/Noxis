/**
 * @file    shell/cmd_uptime.c
 * @brief   `uptime` — seconds since boot from PIT tick counter.
 */
#include <shell/shell.h>
#include <drivers/vga.h>
#include <drivers/pit.h>

static void run(const uint8_t* args) {
    (void)args;
    uint32_t s = pit_uptime_ms() / 1000;
    shell_indent();
    shell_print_u32(s, 0, VGA_YELLOW);
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_write((const uint8_t*)" seconds since boot\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

const shell_cmd_t cmd_uptime = {
    .name  = (const uint8_t*)"uptime",
    .usage = (const uint8_t*)"seconds since boot",
    .run   = run,
};
