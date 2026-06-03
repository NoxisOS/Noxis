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

    puts_at(2, "GDT + IDT loaded (64-bit).", 0x0B);
    serial_write("[noxis64] core up, halting\n");

    for (;;) __asm__ __volatile__("hlt");
}
