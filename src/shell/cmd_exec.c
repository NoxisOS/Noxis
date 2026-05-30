/**
 * @file    shell/cmd_exec.c
 * @brief   `exec <file> [args…]` — load a NoxFS-resident ELF and run it
 *          in ring 3 with an argv[] passed via the user stack.
 */
#include <shell/shell.h>
#include <drivers/vga.h>
#include <fs/vfs.h>
#include <proc/exec.h>
#include <common/status.h>

#define MAX_ARGV       8
#define ARG_STORAGE    256

/* Split a space-separated args string in place into argv[] pointers backed
   by arg_storage. Returns argc. argv[0] is set to the program name. */
static uint32_t _split(const uint8_t* in,
                       uint8_t* storage, uint32_t storage_sz,
                       const uint8_t** argv, uint32_t max_argv) {
    uint32_t argc = 0;
    uint32_t pos  = 0;
    const uint8_t* p = in;

    while (*p && argc < max_argv) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = &storage[pos];
        while (*p && *p != ' ' && pos < storage_sz - 1) {
            storage[pos++] = *p++;
        }
        if (pos < storage_sz) storage[pos++] = 0;
        else break;
    }
    return argc;
}

static void run(const uint8_t* args) {
    if (args[0] == 0) {
        shell_err_usage((const uint8_t*)"exec <file> [args...]");
        return;
    }

    uint8_t        storage[ARG_STORAGE];
    const uint8_t* argv[MAX_ARGV];
    uint32_t       argc = _split(args, storage, ARG_STORAGE, argv, MAX_ARGV);
    if (argc == 0) {
        shell_err_usage((const uint8_t*)"exec <file> [args...]");
        return;
    }

    const vfs_file_t* f = vfs_lookup(argv[0]);
    if (!f) { shell_err_nofile(argv[0]); return; }

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    shell_indent();
    vga_write((const uint8_t*)"exec ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_write(args);
    vga_put_char('\n');
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    int code = 0;
    os_status_t s = exec_run(f->data, f->size, argc, argv, &code);
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
    .usage = (const uint8_t*)"exec <f> [args] run an ELF in ring 3",
    .run   = run,
};
