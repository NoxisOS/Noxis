/**
 * @file    src/boot64/gdt.c
 * @brief   64-bit GDT + TSS.
 *
 * Layout (selectors):
 *   0x00 null
 *   0x08 kernel code (64-bit, ring 0)
 *   0x10 kernel data (ring 0)
 *   0x18 user   code (64-bit, ring 3)
 *   0x20 user   data (ring 3)
 *   0x28 TSS    (16-byte system descriptor)
 */
#include "types.h"

struct __attribute__((packed)) gdt_ptr {
    uint16_t limit;
    uint64_t base;
};

/* 64-bit TSS — only RSP0 and IST entries matter for us. */
struct __attribute__((packed)) tss64 {
    uint32_t reserved0;
    uint64_t rsp[3];        /* RSP0..RSP2 */
    uint64_t reserved1;
    uint64_t ist[7];        /* IST1..IST7 */
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
};

static uint64_t       g_gdt[7];     /* 5 segs + 2 quads for the TSS desc */
static struct tss64   g_tss;
static uint8_t        g_ist1[4096] __attribute__((aligned(16)));  /* IST stack */

extern void gdt64_load(struct gdt_ptr* p);   /* gdt.asm: lgdt + reload segs */
extern void tss64_load(uint16_t sel);         /* gdt.asm: ltr               */

/* Build a standard 8-byte segment descriptor. */
static uint64_t seg(uint8_t access, uint8_t flags) {
    uint64_t d = 0;
    d |= (uint64_t)access << 40;
    d |= (uint64_t)(flags & 0x0F) << 52;   /* limit high + flags nibble */
    return d;
}

void gdt64_init(void) {
    g_gdt[0] = 0;                       /* null */
    /* access: P=1 DPL S=1 E=1 RW=1 ; flags: L=1 (64-bit code) */
    g_gdt[1] = seg(0x9A, 0xA);          /* kernel code */
    g_gdt[2] = seg(0x92, 0xA);          /* kernel data */
    g_gdt[3] = seg(0xFA, 0xA);          /* user code (DPL=3) */
    g_gdt[4] = seg(0xF2, 0xA);          /* user data (DPL=3) */

    /* ── TSS descriptor (16 bytes → two GDT slots) ───────────── */
    for (uint64_t i = 0; i < sizeof(g_tss); i++) ((uint8_t*)&g_tss)[i] = 0;
    g_tss.rsp[0]     = (uint64_t)(g_ist1 + sizeof(g_ist1));
    g_tss.ist[0]     = (uint64_t)(g_ist1 + sizeof(g_ist1));
    g_tss.iomap_base = sizeof(struct tss64);

    uint64_t base  = (uint64_t)&g_tss;
    uint64_t limit = sizeof(struct tss64) - 1;
    uint64_t lo = 0;
    lo |= (limit & 0xFFFF);
    lo |= (base & 0xFFFFFF) << 16;
    lo |= (uint64_t)0x89 << 40;          /* type=available 64-bit TSS, P=1 */
    lo |= ((limit >> 16) & 0xF) << 48;
    lo |= ((base >> 24) & 0xFF) << 56;
    g_gdt[5] = lo;
    g_gdt[6] = (base >> 32) & 0xFFFFFFFF; /* high 32 bits of base */

    struct gdt_ptr p = { sizeof(g_gdt) - 1, (uint64_t)g_gdt };
    gdt64_load(&p);
    tss64_load(0x28);
}
