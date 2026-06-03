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
#include <kernel/hal/fpu.h>
#include <kernel/hal/ports.h>
#include <kernel/isr/isr.h>
#include <mm/phys/pmm.h>
#include <mm/virt/vmm.h>
#include <mm/virt/heap.h>
#include <proc/scheduler.h>

void serial_write_hex64(uint64_t v);

int32_t kbd_poll(void);
os_status_t kbd_init(void);

/* Two test kernel threads — prove preemptive multitasking. */
static void thread_a(void) {
    for (;;) {
        serial_write((const uint8_t*)"[A]");
        for (volatile uint64_t i = 0; i < 8000000ULL; i++) { }
    }
}
static void thread_b(void) {
    for (;;) {
        serial_write((const uint8_t*)"[B]");
        for (volatile uint64_t i = 0; i < 8000000ULL; i++) { }
    }
}

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

    serial_write((const uint8_t*)"[noxis64] FPU ... ");
    fpu_init();
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

    /* ── Scheduler + preemptive multitasking test ─────────────── */
    serial_write((const uint8_t*)"[noxis64] SCHED ... ");
    scheduler_init();
    scheduler_spawn((const uint8_t*)"a", thread_a, 1);
    scheduler_spawn((const uint8_t*)"b", thread_b, 1);
    pit_set_tick_cb(scheduler_tick);          /* PIT preempts threads */
    serial_write((const uint8_t*)"OK\n");

    cpu_sti();

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_write((const uint8_t*)"core up + preemptive scheduler (see serial [A]/[B])\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    serial_write((const uint8_t*)"[noxis64] core up, scheduling threads\n");

    /* The boot context becomes the idle thread; PIT preemption rotates
       between idle, thread A and thread B. */
    for (;;) __asm__ __volatile__("hlt");
}
