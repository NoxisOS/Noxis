/**
 * @file    shell/commands/cmd_memstat.c
 * @brief   memstat — slab cache and heap statistics.
 *
 * Shows live/free counts for each slab cache and kernel heap free space.
 * Use this to verify the slab+arena allocators work and detect leaks:
 *   1. Run `memstat`   → note live counts
 *   2. Fork some processes, run programs
 *   3. Run `memstat`   → live counts should return to baseline after exit
 */
#include <shell/shell.h>
#include <mm/slab.h>
#include <mm/virt/heap.h>
#include <drivers/vga.h>

static void _pu(uint32_t v) {
    if (v >= 10) _pu(v / 10);
    vga_put_char('0' + (v % 10));
}

static void _slab_row(const slab_cache_t *c) {
    if (!c) {
        shell_indent();
        vga_write((const uint8_t*)"(uninitialised)\n");
        return;
    }
    shell_indent();
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_write((const uint8_t*)c->name);
    vga_pad_to(28, ' ');
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_write((const uint8_t*)"live=");
    shell_print_u32(c->n_alloc, 3, VGA_YELLOW);
    vga_write((const uint8_t*)" free=");
    shell_print_u32(c->n_free,  3, VGA_LIGHT_GREY);
    vga_write((const uint8_t*)" peak=");
    shell_print_u32(c->n_alloc_peak, 3, VGA_LIGHT_MAGENTA);
    vga_write((const uint8_t*)" obj=");
    _pu(c->obj_size);
    vga_write((const uint8_t*)"B\n");
}

static void _tag_row(mem_tag_t t) {
    uint32_t b = heap_tag_bytes(t);
    uint32_t n = heap_tag_allocs(t);
    if (b == 0 && n == 0) return;   /* skip empty tags */
    shell_indent();
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_write((const uint8_t*)heap_tag_name(t));
    vga_pad_to(28, ' ');
    vga_set_color(VGA_WHITE, VGA_BLACK);
    shell_print_u32(b, 6, VGA_LIGHT_GREEN);
    vga_write((const uint8_t*)" B  (");
    _pu(n);
    vga_write((const uint8_t*)" allocs)\n");
}

static void run(const uint8_t *args) {
    /* `memstat reap` releases idle slab blocks back to the heap. */
    if (args && args[0] == 'r') {
        uint32_t freed = slab_reap_all();
        vga_set_color(VGA_WHITE, VGA_BLACK);
        shell_indent();
        vga_write((const uint8_t*)"reaped ");
        shell_print_u32(freed, 0, VGA_LIGHT_GREEN);
        vga_write((const uint8_t*)" bytes from slab caches\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }

    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_write((const uint8_t*)"slab caches:\n");
    _slab_row(g_process_slab);
    _slab_row(g_pipe_slab);

    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_write((const uint8_t*)"heap by subsystem:\n");
    for (uint32_t t = 0; t < MEM_TAG__COUNT; t++)
        _tag_row((mem_tag_t)t);

    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_write((const uint8_t*)"kernel heap:\n");
    shell_indent();
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_write((const uint8_t*)"free=");
    shell_print_u32(heap_get_free() / 1024, 0, VGA_LIGHT_GREEN);
    vga_write((const uint8_t*)" KB\n");

    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

const shell_cmd_t cmd_memstat = {
    .name  = (const uint8_t*)"memstat",
    .usage = (const uint8_t*)"show slab cache and heap stats",
    .run   = run,
};
