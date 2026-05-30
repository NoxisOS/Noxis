/**
 * @file    shell/shell.c
 * @brief   REPL + command registry + parsing.
 *
 * Adding a builtin:
 *   1. create src/shell/cmd_<name>.c that defines
 *      `const shell_cmd_t cmd_<name>`
 *   2. add `extern const shell_cmd_t cmd_<name>;` below
 *   3. add `&cmd_<name>,` to g_cmds[]
 *   4. add the .o to KERNEL_C_OBJS in the Makefile
 *
 * @author  Noxis Team
 * @date    2026-05-30
 */
#include <shell/shell.h>
#include <drivers/vga.h>
#include <drivers/tty/tty.h>
#include <common/types.h>

#define LINE_MAX  128
#define NAME_MAX  32

/* ── registry ───────────────────────────────────────────────── */

extern const shell_cmd_t cmd_help;
extern const shell_cmd_t cmd_uptime;
extern const shell_cmd_t cmd_ls;
extern const shell_cmd_t cmd_cat;
extern const shell_cmd_t cmd_exec;
extern const shell_cmd_t cmd_clear;
extern const shell_cmd_t cmd_halt;
extern const shell_cmd_t cmd_sleep;
extern const shell_cmd_t cmd_cd;
extern const shell_cmd_t cmd_mkdir;

static const shell_cmd_t* g_cmds[] = {
    &cmd_help,
    &cmd_uptime,
    &cmd_ls,
    &cmd_cat,
    &cmd_exec,
    &cmd_cd,
    &cmd_mkdir,
    &cmd_clear,
    &cmd_halt,
    &cmd_sleep,
};

#define G_CMDS_N (sizeof(g_cmds) / sizeof(g_cmds[0]))

uint32_t shell_cmd_count(void) { return G_CMDS_N; }

const shell_cmd_t* shell_cmd_at(uint32_t i) {
    return i < G_CMDS_N ? g_cmds[i] : (const shell_cmd_t*)0;
}

/* ── shared helpers ─────────────────────────────────────────── */

void shell_indent(void) {
    for (uint32_t i = 0; i < SHELL_INDENT; i++) vga_put_char(' ');
}

void shell_err_nofile(const uint8_t* name) {
    vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
    shell_indent();
    vga_write((const uint8_t*)"no such file: ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_write(name);
    vga_put_char('\n');
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

void shell_err_usage(const uint8_t* usage) {
    vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
    shell_indent();
    vga_write((const uint8_t*)"usage: ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_write(usage);
    vga_put_char('\n');
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

void shell_print_u32(uint32_t v, uint32_t width, uint8_t fg) {
    uint8_t buf[12]; uint32_t i = 11; buf[11] = 0;
    if (v == 0) buf[--i] = '0';
    else { while (v) { buf[--i] = (uint8_t)('0' + (v % 10)); v /= 10; } }
    uint32_t len = 11 - i;
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    while (len < width) { vga_put_char(' '); len++; }
    vga_set_color(fg, VGA_BLACK);
    vga_write(&buf[i]);
}

const uint8_t* shell_skip_spaces(const uint8_t* s) {
    while (*s == ' ') s++;
    return s;
}

/* ── parsing + dispatch ─────────────────────────────────────── */

static int _streq(const uint8_t* a, const uint8_t* b) {
    uint32_t i = 0;
    while (a[i] && b[i]) { if (a[i] != b[i]) return 0; i++; }
    return a[i] == b[i];
}

static void _prompt(void) {
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_write((const uint8_t*)"   noxis");
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_write((const uint8_t*)" > ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    /* Prime the attribute at the cursor cell so the blinking hardware cursor
       appears white, not a stale colour left by whatever was there before. */
    vga_prime_cursor();
}

static void _dispatch(const uint8_t* line) {
    line = shell_skip_spaces(line);
    if (line[0] == 0) return;

    /* split: first word = command, rest = args (with leading spaces trimmed) */
    uint8_t name[NAME_MAX];
    uint32_t i = 0;
    while (line[i] && line[i] != ' ' && i < NAME_MAX - 1) {
        name[i] = line[i];
        i++;
    }
    name[i] = 0;
    const uint8_t* args = shell_skip_spaces(line + i);

    for (uint32_t k = 0; k < G_CMDS_N; k++) {
        if (_streq(name, g_cmds[k]->name)) {
            g_cmds[k]->run(args);
            return;
        }
    }

    vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
    shell_indent();
    vga_write((const uint8_t*)"unknown: ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_write(name);
    vga_put_char('\n');
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

void shell_run(void) {
    uint8_t line[LINE_MAX];

    _prompt();
    for (;;) {
        int32_t n = tty_read(line, LINE_MAX - 1);
        if (n <= 0) { _prompt(); continue; }

        line[n] = 0;
        if (n > 0 && line[n - 1] == '\n') line[n - 1] = 0;
        _dispatch(line);
        _prompt();
    }
}
