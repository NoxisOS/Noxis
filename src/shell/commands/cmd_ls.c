/**
 * @file    shell/commands/cmd_ls.c
 * @brief   `ls` — list directory contents using getdents.
 */
#include <shell/shell.h>
#include <drivers/vga.h>
#include <fs/noxfs/noxfs.h>
#include <fs/vfs/vfs.h>
#include <proc/scheduler.h>
#include <common/types.h>

static void run(const uint8_t* args) {
    (void)args;

    process_t* proc = scheduler_current();
    uint32_t dir_ino = proc->cwd_ino;

    uint8_t buf[sizeof(noxfs_dirent_t)];
    uint32_t off = 0;

    for (;;) {
        int32_t n = noxfs_getdents(dir_ino, buf, sizeof(buf), &off);
        if (n <= 0) break;

        noxfs_dirent_t* d = (noxfs_dirent_t*)buf;
        if (d->inode == 0) continue;

        uint8_t type_char = (d->file_type == NOXFS_FT_DIR) ? 'd' :
                            (d->file_type == NOXFS_FT_FILE) ? '-' : '?';

        /* Get size via stat */
        vfs_file_t st;
        uint32_t fsize = 0;
        if (noxfs_stat(d->inode, &st) == OS_OK)
            fsize = st.size;

        shell_indent();
        vga_put_char(type_char);
        vga_put_char(' ');
        shell_print_u32(fsize, 6, VGA_YELLOW);
        vga_write((const uint8_t*)"  ");
        vga_set_color((d->file_type == NOXFS_FT_DIR) ? VGA_LIGHT_BLUE : VGA_LIGHT_CYAN, VGA_BLACK);

        for (uint32_t i = 0; i < d->name_len; i++)
            vga_put_char((uint8_t)d->name[i]);

        vga_put_char('\n');
    }
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

const shell_cmd_t cmd_ls = {
    .name  = (const uint8_t*)"ls",
    .usage = (const uint8_t*)"list directory contents",
    .run   = run,
};
