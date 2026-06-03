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
#include <kernel/hal/gdt.h>
#include <kernel/hal/pic.h>
#include <kernel/hal/ports.h>
#include <kernel/isr/isr.h>
#include <mm/phys/pmm.h>
#include <mm/virt/vmm.h>
#include <mm/virt/heap.h>

void serial_write_hex64(uint64_t v);

#define VGA ((volatile uint16_t*)0xB8000)

static void puts_at(int row, const char* s, uint8_t attr) {
    volatile uint16_t* p = VGA + row * 80;
    for (int i = 0; s[i]; i++) p[i] = ((uint16_t)attr << 8) | (uint8_t)s[i];
}

void kernel_main(void) {
    for (int i = 0; i < 80 * 6; i++) VGA[i] = 0x0700 | ' ';

    serial_init();
    serial_write((const uint8_t*)"\n[noxis64] kernel_main reached\n");
    puts_at(0, "Noxis OS  --  x86_64", 0x0F);

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

    /* ── Timer + interrupts ───────────────────────────────────── */
    serial_write((const uint8_t*)"[noxis64] PIT ... ");
    pit_init(1000);                  /* 1 ms tick */
    cpu_sti();                       /* enable interrupts */
    serial_write((const uint8_t*)"OK\n");

    pit_sleep_ms(50);
    serial_write((const uint8_t*)"[noxis64] uptime after 50ms sleep=");
    serial_write_hex64(pit_uptime_ms());
    serial_write((const uint8_t*)" ticks\n");

    puts_at(1, "core up: GDT IDT PIC PMM VMM HEAP PIT", 0x0A);
    serial_write((const uint8_t*)"[noxis64] core up, idle\n");

    for (;;) __asm__ __volatile__("hlt");
}
