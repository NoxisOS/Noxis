/**
 * @file    shell/commands/cmd_cd.c
 * @brief   `cd <path>` — change working directory.
 */
#include <shell/shell.h>
#include <drivers/vga.h>
#include <fs/noxfs/noxfs.h>
#include <proc/scheduler.h>
#include <common/types.h>

static void run(const uint8_t* args) {
    args = shell_skip_spaces(args);
    if (args[0] == 0) {
        shell_err_usage((const uint8_t*)"cd <path>");
        return;
    }

    process_t* proc = scheduler_current();
    uint32_t base = (args[0] == '/') ? noxfs_root_ino() : proc->cwd_ino;
    uint32_t ino  = noxfs_resolve(base, args);

    if (ino == (uint32_t)-1) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        shell_indent();
        vga_write((const uint8_t*)"cd: no such directory\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }

    proc->cwd_ino = ino;
}

const shell_cmd_t cmd_cd = {
    .name  = (const uint8_t*)"cd",
    .usage = (const uint8_t*)"cd <path>   change working directory",
    .run   = run,
};
