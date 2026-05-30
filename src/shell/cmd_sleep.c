/**
 * @file    shell/cmd_sleep.c
 * @brief   `sleep <ms>` — blocks the shell via thread_sleep for N milliseconds.
 *          Demonstrates: PROC_BLOCKED, 0% CPU, wakeup by tick.
 */
#include <shell/shell.h>
#include <proc/scheduler.h>
#include <drivers/vga.h>
#include <drivers/pit.h>

static uint32_t _parse_u32(const uint8_t* s) {
    uint32_t v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (uint32_t)(*s - '0');
        s++;
    }
    return v;
}

static void run(const uint8_t* args) {
    uint32_t ms = _parse_u32(args);
    if (ms == 0) ms = 1000;

    shell_indent();
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_write((const uint8_t*)"sleeping for ");
    shell_print_u32(ms, 0, VGA_WHITE);
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_write((const uint8_t*)" ms");
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_write((const uint8_t*)" (other threads can run)\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    uint32_t before = pit_uptime_ms();
    thread_sleep(ms);
    uint32_t elapsed = pit_uptime_ms() - before;

    shell_indent();
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_write((const uint8_t*)"done \xc4 slept ");
    shell_print_u32(elapsed, 0, VGA_WHITE);
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_write((const uint8_t*)" ms\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

const shell_cmd_t cmd_sleep = {
    .name  = (const uint8_t*)"sleep",
    .usage = (const uint8_t*)"sleep <ms> — blocking sleep (0% CPU)",
    .run   = run,
};
