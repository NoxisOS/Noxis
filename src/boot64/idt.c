/**
 * @file    src/boot64/idt.c
 * @brief   64-bit IDT: 256 gates, 16 bytes each.
 */
#include "types.h"

struct __attribute__((packed)) idt_entry {
    uint16_t off_lo;
    uint16_t sel;
    uint8_t  ist;
    uint8_t  type;
    uint16_t off_mid;
    uint32_t off_hi;
    uint32_t reserved;
};

struct __attribute__((packed)) idt_ptr {
    uint16_t limit;
    uint64_t base;
};

static struct idt_entry g_idt[256];

extern void idt64_load(struct idt_ptr* p);   /* idt.asm */
extern void* isr_stub_table[];                /* isr.asm: 32 exception stubs */

static void set_gate(int n, void* handler, uint8_t type) {
    uint64_t h = (uint64_t)handler;
    g_idt[n].off_lo  = h & 0xFFFF;
    g_idt[n].sel     = 0x08;             /* kernel code */
    g_idt[n].ist     = 0;
    g_idt[n].type    = type;             /* 0x8E = present, ring0, interrupt gate */
    g_idt[n].off_mid = (h >> 16) & 0xFFFF;
    g_idt[n].off_hi  = (h >> 32) & 0xFFFFFFFF;
    g_idt[n].reserved = 0;
}

void idt64_init(void) {
    for (int i = 0; i < 256; i++) set_gate(i, (void*)0, 0);
    /* CPU exceptions 0..31 */
    for (int i = 0; i < 32; i++) set_gate(i, isr_stub_table[i], 0x8E);

    struct idt_ptr p = { sizeof(g_idt) - 1, (uint64_t)g_idt };
    idt64_load(&p);
}
