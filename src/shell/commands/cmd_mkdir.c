/**
 * @file    shell/commands/cmd_mkdir.c
 * @brief   `mkdir <name>` — create a directory.
 */
#include <shell/shell.h>
#include <drivers/vga.h>
#include <fs/noxfs/noxfs.h>
#include <proc/scheduler.h>
#include <common/types.h>

static void run(const uint8_t* args) {
    args = shell_skip_spaces(args);
    if (args[0] == 0) {
        shell_err_usage((const uint8_t*)"mkdir <name>");
        return;
    }

    process_t* proc = scheduler_current();
    uint32_t ino = noxfs_mkdir(proc->cwd_ino, args);

    if (ino == (uint32_t)-1) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        shell_indent();
        vga_write((const uint8_t*)"mkdir: failed\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }

    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    shell_indent();
    vga_write((const uint8_t*)"created ");
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_write(args);
    vga_put_char('\n');
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

const shell_cmd_t cmd_mkdir = {
    .name  = (const uint8_t*)"mkdir",
    .usage = (const uint8_t*)"mkdir <name> create a directory",
    .run   = run,
};
