/**
 * @file    shell/cmd_halt.c
 * @brief   `halt` — flush VFS, then shutdown via ACPI.
 */
#include <shell/shell.h>
#include <drivers/vga.h>
#include <kernel/hal/ports.h>
#include <fs/vfs/vfs.h>

static void run(const uint8_t* args) {
    (void)args;
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    shell_indent();
    vga_write((const uint8_t*)"syncing disk...\n");
    vfs_sync();

    shell_indent();
    vga_write((const uint8_t*)"shutting down.\n");

    /* QEMU ACPI shutdown — port 0x604 value 0x2000 */
    port_word_out(0x604, 0x2000);

    /* Fallback: park the CPU */
    cpu_cli();
    for (;;) cpu_hlt();
}

const shell_cmd_t cmd_halt = {
    .name  = (const uint8_t*)"halt",
    .usage = (const uint8_t*)"stop the CPU",
    .run   = run,
};
