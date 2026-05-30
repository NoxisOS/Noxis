/**
 * @file    shell/cmd_clear.c
 * @brief   `clear` — wipe the screen.
 */
#include <shell/shell.h>
#include <drivers/vga.h>

static void run(const uint8_t* args) {
    (void)args;
    vga_clear();
}

const shell_cmd_t cmd_clear = {
    .name  = (const uint8_t*)"clear",
    .usage = (const uint8_t*)"wipe the screen",
    .run   = run,
};
