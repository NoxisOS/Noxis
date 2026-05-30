/**
 * @file    shell/commands/cmd_blkstat.c
 * @brief   `blkstat` — list registered block devices and request stats.
 */
#include <shell/shell.h>
#include <drivers/vga.h>
#include <drivers/block/block.h>

static void run(const uint8_t* args) {
    (void)args;

    int n = blk_device_count();

    shell_indent();
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_write((const uint8_t*)"block devices: ");
    shell_print_u32((uint32_t)n, 0, VGA_YELLOW);
    vga_put_char('\n');

    for (int i = 0; i < n; i++) {
        block_device_t* d = blk_get(i);
        if (!d) continue;

        shell_indent();
        vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
        vga_write((const uint8_t*)d->name);
        vga_set_color(VGA_DARK_GREY, VGA_BLACK);
        vga_write((const uint8_t*)"  sectors=");
        if (d->sectors) {
            shell_print_u32(d->sectors, 0, VGA_WHITE);
            vga_set_color(VGA_DARK_GREY, VGA_BLACK);
            vga_write((const uint8_t*)"  size=");
            shell_print_u32(d->sectors / 2, 0, VGA_WHITE);  /* KiB (512B/sec) */
            vga_set_color(VGA_DARK_GREY, VGA_BLACK);
            vga_write((const uint8_t*)" KiB");
        } else {
            vga_set_color(VGA_WHITE, VGA_BLACK);
            vga_write((const uint8_t*)"unknown");
        }
        vga_put_char('\n');
    }

    shell_indent();
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_write((const uint8_t*)"requests served: ");
    shell_print_u32(blk_requests_served(), 0, VGA_YELLOW);
    vga_put_char('\n');

    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

const shell_cmd_t cmd_blkstat = {
    .name  = (const uint8_t*)"blkstat",
    .usage = (const uint8_t*)"list block devices + I/O stats",
    .run   = run,
};
