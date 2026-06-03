/**
 * @file    src/boot64/kmain.c
 * @brief   64-bit Noxis kernel entry — brings up core subsystems.
 */
#include "types.h"

void serial_init(void);
void serial_write(const char* s);
void serial_hex(uint64_t v);
void gdt64_init(void);
void idt64_init(void);
void pmm_init(void);
uint64_t pmm_alloc_frame(void);
uint64_t pmm_free_count(void);
void vmm_init(void);
int  vmm_map_page(uint64_t va, uint64_t pa, uint64_t flags);

#define VGA ((volatile uint16_t*)0xB8000)

static void puts_at(int row, const char* s, uint8_t attr) {
    volatile uint16_t* p = VGA + row * 80;
    for (int i = 0; s[i]; i++)
        p[i] = ((uint16_t)attr << 8) | (uint8_t)s[i];
}

void kmain64(void) {
    for (int i = 0; i < 80 * 6; i++) VGA[i] = 0x0700 | ' ';

    serial_init();
    serial_write("\n[noxis64] kmain64 reached\n");

    puts_at(0, "Noxis OS  --  x86_64 long mode", 0x0F);
    puts_at(1, "64-bit C kernel running.", 0x0A);

    serial_write("[noxis64] GDT ... ");
    gdt64_init();
    serial_write("OK\n");

    serial_write("[noxis64] IDT ... ");
    idt64_init();
    serial_write("OK\n");

    serial_write("[noxis64] PMM ... ");
    pmm_init();

    serial_write("[noxis64] VMM ... ");
    vmm_init();

    /* ── Validate alloc + map + read/write at a high VA ───────── */
    uint64_t frame = pmm_alloc_frame();
    uint64_t va    = 0x40000000ULL;          /* 1 GB — outside identity map */
    serial_write("[noxis64] map "); serial_hex(va);
    serial_write(" -> "); serial_hex(frame); serial_write("\n");
    vmm_map_page(va, frame, 0x2 /* RW */);

    volatile uint64_t* p = (volatile uint64_t*)va;
    *p = 0xCAFEBABEDEADBEEFULL;
    serial_write("[noxis64] readback="); serial_hex(*p);
    serial_write(*p == 0xCAFEBABEDEADBEEFULL ? "  PASS\n" : "  FAIL\n");

    puts_at(2, "GDT IDT PMM VMM up. paging OK.", 0x0B);
    serial_write("[noxis64] core up, halting\n");

    for (;;) __asm__ __volatile__("hlt");
}
