/**
 * @file    shell/cmd_exec.c
 * @brief   `exec <file>` — load a NoxFS-resident ELF and run it in ring 3.
 */
#include <shell/shell.h>
#include <drivers/vga.h>
#include <fs/vfs.h>
#include <proc/exec.h>
#include <common/status.h>

static void run(const uint8_t* args) {
    if (args[0] == 0) {
        shell_err_usage((const uint8_t*)"exec <file>");
        return;
    }
    const vfs_file_t* f = vfs_lookup(args);
    if (!f) { shell_err_nofile(args); return; }

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    shell_indent();
    vga_write((const uint8_t*)"exec ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_write(args);
    vga_put_char('\n');
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    int code = 0;
    os_status_t s = exec_run(f->data, f->size, &code);
    if (s != OS_OK) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        shell_indent();
        vga_write((const uint8_t*)"exec failed (bad ELF or OOM)\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }

    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    shell_indent();
    vga_write((const uint8_t*)"[exited ");
    shell_print_u32((uint32_t)code, 0, VGA_YELLOW);
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_write((const uint8_t*)"]\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

const shell_cmd_t cmd_exec = {
    .name  = (const uint8_t*)"exec",
    .usage = (const uint8_t*)"exec <file>    run an ELF in ring 3",
    .run   = run,
};
