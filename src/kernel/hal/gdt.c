/**
 * @file    hal/gdt.c
 * @brief   64-bit GDT + TSS.
 *
 *   0x08 kernel code | 0x10 kernel data | 0x1B user code | 0x23 user data
 *   0x28 TSS (16-byte system descriptor)
 */
#include <kernel/hal/gdt.h>
#include <common/types.h>

struct __attribute__((packed)) gdt_ptr { uint16_t limit; uint64_t base; };

struct __attribute__((packed)) tss64 {
    uint32_t reserved0;
    uint64_t rsp[3];
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
};

static uint64_t      g_gdt[7];
static struct tss64  g_tss;
static uint8_t       g_ist1[4096] __attribute__((aligned(16)));

extern void gdt64_load(struct gdt_ptr* p);   /* gdt_load.asm */
extern void tss64_load(uint16_t sel);

static uint64_t seg(uint8_t access, uint8_t flags) {
    return ((uint64_t)access << 40) | ((uint64_t)(flags & 0x0F) << 52);
}

void gdt_set_kernel_stack(uint64_t rsp) { g_tss.rsp[0] = rsp; }

os_status_t gdt_init(void) {
    g_gdt[0] = 0;
    /* Order matters for SYSRET: user DATA (0x18) must precede user CODE
       (0x20).  SYSRET loads SS = STAR_SYSRET+8 and CS = STAR_SYSRET+16. */
    g_gdt[1] = seg(0x9A, 0xA);   /* 0x08 kernel code, L=1 */
    g_gdt[2] = seg(0x92, 0xA);   /* 0x10 kernel data      */
    g_gdt[3] = seg(0xF2, 0xA);   /* 0x18 user data,  DPL3 */
    g_gdt[4] = seg(0xFA, 0xA);   /* 0x20 user code,  DPL3, L=1 */

    for (uint64_t i = 0; i < sizeof(g_tss); i++) ((uint8_t*)&g_tss)[i] = 0;
    g_tss.rsp[0]     = (uint64_t)(g_ist1 + sizeof(g_ist1));
    g_tss.ist[0]     = (uint64_t)(g_ist1 + sizeof(g_ist1));
    g_tss.iomap_base = sizeof(struct tss64);

    uint64_t base  = (uint64_t)&g_tss;
    uint64_t limit = sizeof(struct tss64) - 1;
    uint64_t lo = (limit & 0xFFFF)
                | ((base & 0xFFFFFF) << 16)
                | ((uint64_t)0x89 << 40)
                | (((limit >> 16) & 0xF) << 48)
                | (((base >> 24) & 0xFF) << 56);
    g_gdt[5] = lo;
    g_gdt[6] = (base >> 32) & 0xFFFFFFFF;

    struct gdt_ptr p = { sizeof(g_gdt) - 1, (uint64_t)g_gdt };
    gdt64_load(&p);
    tss64_load(SEL_TSS);
    return OS_OK;
}
