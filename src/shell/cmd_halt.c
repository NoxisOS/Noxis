/**
 * @file    shell/cmd_halt.c
 * @brief   `halt` — disable interrupts and park the CPU forever.
 */
#include <shell/shell.h>
#include <drivers/vga.h>
#include <hal/ports.h>

static void run(const uint8_t* args) {
    (void)args;
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    shell_indent();
    vga_write((const uint8_t*)"halted.\n");
    cpu_cli();
    for (;;) cpu_hlt();
}

const shell_cmd_t cmd_halt = {
    .name  = (const uint8_t*)"halt",
    .usage = (const uint8_t*)"stop the CPU",
    .run   = run,
};
