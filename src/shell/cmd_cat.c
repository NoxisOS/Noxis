/**
 * @file    shell/cmd_cat.c
 * @brief   `cat <file>` — dump a VFS file to the screen, indented.
 */
#include <shell/shell.h>
#include <drivers/vga.h>
#include <fs/vfs.h>

static void run(const uint8_t* args) {
    if (args[0] == 0) {
        shell_err_usage((const uint8_t*)"cat <file>");
        return;
    }
    const vfs_file_t* f = vfs_lookup(args);
    if (!f) { shell_err_nofile(args); return; }

    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    shell_indent();
    for (uint32_t i = 0; i < f->size; i++) {
        uint8_t c = f->data[i];
        vga_put_char(c);
        if (c == '\n' && i + 1 < f->size) shell_indent();
    }
    if (f->size == 0 || f->data[f->size - 1] != '\n') vga_put_char('\n');
}

const shell_cmd_t cmd_cat = {
    .name  = (const uint8_t*)"cat",
    .usage = (const uint8_t*)"cat <file>     dump file contents",
    .run   = run,
};
