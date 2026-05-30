/**
 * @file    shell/cmd_ls.c
 * @brief   `ls` — list files in the active VFS mount with byte size.
 */
#include <shell/shell.h>
#include <drivers/vga.h>
#include <fs/vfs.h>

static void run(const uint8_t* args) {
    (void)args;
    uint32_t n = vfs_count();
    for (uint32_t i = 0; i < n; i++) {
        const vfs_file_t* f = vfs_entry(i);
        shell_indent();
        shell_print_u32(f->size, 6, VGA_YELLOW);
        vga_write((const uint8_t*)"  ");
        vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
        vga_write(f->name);
        vga_put_char('\n');
    }
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

const shell_cmd_t cmd_ls = {
    .name  = (const uint8_t*)"ls",
    .usage = (const uint8_t*)"list files in the VFS",
    .run   = run,
};
