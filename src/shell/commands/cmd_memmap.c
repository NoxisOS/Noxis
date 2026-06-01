/**
 * @file    shell/commands/cmd_memmap.c
 * @brief   memmap — live physical-memory heatmap rendered in VGA.
 *
 * Draws the PMM frame bitmap as a 64×16 grid of colored blocks. Each
 * cell aggregates a contiguous range of physical frames and is colored
 * by the "most interesting" state present in that range:
 *
 *   dim grey  free
 *   red       kernel / BIOS reserved (low memory)
 *   green     user-allocated
 *   magenta   copy-on-write shared (refcount >= 2)
 *
 * This makes the effect of fork (CoW), exec, and allocation visible at
 * a glance — run `memmap`, fork a process, run it again, watch the
 * shared pages light up magenta.
 */
#include <shell/shell.h>
#include <mm/phys/pmm.h>
#include <mm/virt/paging.h>
#include <drivers/vga.h>

#define GRID_COLS   64
#define GRID_ROWS   16
#define GRID_CELLS  (GRID_COLS * GRID_ROWS)

/* Frames below this index are kernel / BIOS reserved (see pmm_init:
   it marks 0 .. 0x00500000 as used). */
#define KERNEL_FRAME_END  (0x00500000u / PAGE_SIZE)

#define BLOCK_CHAR  0xDB   /* CP437 full block █ */

/* Cell states, ordered by display priority (highest wins). */
enum { ST_FREE = 0, ST_KERNEL, ST_USER, ST_COW };

static uint8_t _state_color(int st) {
    switch (st) {
        case ST_COW:    return VGA_LIGHT_MAGENTA;
        case ST_USER:   return VGA_LIGHT_GREEN;
        case ST_KERNEL: return VGA_LIGHT_RED;
        default:        return VGA_DARK_GREY;
    }
}

static void _legend_item(int st, const uint8_t *label) {
    vga_set_color(_state_color(st), VGA_BLACK);
    vga_put_char(BLOCK_CHAR);
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_put_char(' ');
    vga_write(label);
    vga_write((const uint8_t*)"  ");
}

static void run(const uint8_t *args) {
    (void)args;

    uint32_t total = pmm_get_total_frames();
    uint32_t fpc   = (total + GRID_CELLS - 1) / GRID_CELLS;  /* frames/cell */
    if (fpc == 0) fpc = 1;

    /* ── Title ───────────────────────────────────────────────── */
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_write((const uint8_t*)"\n  physical memory map  (");
    shell_print_u32(fpc, 0, VGA_YELLOW);
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_write((const uint8_t*)" frames/cell, ");
    shell_print_u32(fpc * 4, 0, VGA_YELLOW);
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_write((const uint8_t*)" KB/cell)\n\n");

    /* ── Grid ────────────────────────────────────────────────── */
    for (uint32_t row = 0; row < GRID_ROWS; row++) {
        vga_write((const uint8_t*)"  ");
        for (uint32_t col = 0; col < GRID_COLS; col++) {
            uint32_t cell  = row * GRID_COLS + col;
            uint32_t start = cell * fpc;

            int st = ST_FREE;
            for (uint32_t f = start; f < start + fpc && f < total; f++) {
                if (!pmm_frame_used(f)) continue;

                int s;
                if (f < KERNEL_FRAME_END)            s = ST_KERNEL;
                else if (pmm_ref_count(f * PAGE_SIZE) >= 2) s = ST_COW;
                else                                  s = ST_USER;

                if (s > st) st = s;   /* keep highest-priority state */
            }

            vga_set_color(_state_color(st), VGA_BLACK);
            vga_put_char(BLOCK_CHAR);
        }
        vga_put_char('\n');
    }

    /* ── Legend ──────────────────────────────────────────────── */
    vga_put_char('\n');
    vga_write((const uint8_t*)"  ");
    _legend_item(ST_FREE,   (const uint8_t*)"free");
    _legend_item(ST_KERNEL, (const uint8_t*)"kernel");
    _legend_item(ST_USER,   (const uint8_t*)"user");
    _legend_item(ST_COW,    (const uint8_t*)"cow-shared");
    vga_put_char('\n');

    /* ── Summary ─────────────────────────────────────────────── */
    uint32_t used = total - pmm_get_free_count();
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_write((const uint8_t*)"  ");
    shell_print_u32(used, 0, VGA_LIGHT_GREEN);
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_write((const uint8_t*)" / ");
    shell_print_u32(total, 0, VGA_WHITE);
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_write((const uint8_t*)" frames used  (");
    shell_print_u32(used * 4 / 1024, 0, VGA_LIGHT_GREEN);
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_write((const uint8_t*)" / ");
    shell_print_u32(total * 4 / 1024, 0, VGA_WHITE);
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_write((const uint8_t*)" MB)\n");

    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

const shell_cmd_t cmd_memmap = {
    .name  = (const uint8_t*)"memmap",
    .usage = (const uint8_t*)"live physical memory heatmap",
    .run   = run,
};
