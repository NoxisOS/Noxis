/**
 * @file    kernel/core/early.c
 * @brief   64-bit kernel entry — brings up the core subsystems.
 *
 * NOTE: This is the x86_64 port in progress. The full init sequence
 * (scheduler, syscalls, VFS, userland…) is being ported phase by phase;
 * for now kernel_main brings up GDT/IDT/PMM/VMM/heap and halts.
 */
#include <common/types.h>
#include <drivers/serial.h>
#include <drivers/pit.h>
#include <drivers/vga.h>
#include <drivers/kbd.h>
#include <drivers/keymap.h>
#include <kernel/hal/gdt.h>
#include <kernel/hal/pic.h>
#include <kernel/hal/ports.h>
#include <kernel/isr/isr.h>
#include <mm/phys/pmm.h>
#include <mm/virt/vmm.h>
#include <mm/virt/heap.h>

void serial_write_hex64(uint64_t v);

int32_t kbd_poll(void);
os_status_t kbd_init(void);

void kernel_main(void) {
    vga_init();
    vga_clear();

    serial_init();
    serial_write((const uint8_t*)"\n[noxis64] kernel_main reached\n");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_write((const uint8_t*)"Noxis OS  --  x86_64\n");

    serial_write((const uint8_t*)"[noxis64] GDT ... ");
    gdt_init();
    serial_write((const uint8_t*)"OK\n");

    serial_write((const uint8_t*)"[noxis64] IDT ... ");
    isr_init();
    serial_write((const uint8_t*)"OK\n");

    serial_write((const uint8_t*)"[noxis64] PIC ... ");
    pic_remap();
    serial_write((const uint8_t*)"OK\n");

    pmm_init(128ULL * 1024 * 1024);
    vmm_init();
    heap_init();

    /* Smoke test: alloc + map + heap. */
    uint64_t fr = pmm_alloc_frame();
    vmm_map_page(0x40000000ULL, fr, PAGE_RW);
    volatile uint64_t* t = (volatile uint64_t*)0x40000000ULL;
    *t = 0xCAFEBABEDEADBEEFULL;
    serial_write((const uint8_t*)"[noxis64] paging test ");
    serial_write((const uint8_t*)(*t == 0xCAFEBABEDEADBEEFULL ? "PASS\n" : "FAIL\n"));

    void* a = kmalloc(64);
    kfree(a);
    serial_write((const uint8_t*)"[noxis64] heap test ");
    serial_write((const uint8_t*)(a ? "PASS\n" : "FAIL\n"));

    /* ── Timer + keyboard + interrupts ────────────────────────── */
    serial_write((const uint8_t*)"[noxis64] PIT ... ");
    pit_init(1000);
    serial_write((const uint8_t*)"OK\n");

    serial_write((const uint8_t*)"[noxis64] KEYMAP ... ");
    keymap_init();
    serial_write((const uint8_t*)"OK\n");

    serial_write((const uint8_t*)"[noxis64] KBD ... ");
    kbd_init();
    serial_write((const uint8_t*)"OK\n");

    cpu_sti();

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_write((const uint8_t*)"core up: GDT IDT PIC PMM VMM HEAP PIT KBD\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_write((const uint8_t*)"type something (keyboard echo):\n");
    serial_write((const uint8_t*)"[noxis64] core up, keyboard echo loop\n");

    /* Interactive echo loop — proves keyboard IRQ + VGA output. */
    for (;;) {
        int32_t c = kbd_poll();
        if (c < 0) { __asm__ __volatile__("hlt"); continue; }
        if (c == '\b') vga_backspace();
        else           vga_put_char((uint8_t)c);
    }
}
