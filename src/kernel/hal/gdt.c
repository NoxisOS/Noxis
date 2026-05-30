/**
 * @file    hal/gdt.c
 * @brief   Global Descriptor Table — segments + TSS
 * @author  Noxis Team
 * @date    2026-05-29
 */
#include <kernel/hal/gdt.h>
#include <common/types.h>

static gdt_entry_t g_gdt[GDT_ENTRIES];
static gdt_ptr_t   g_gdt_ptr;
static tss_t       g_tss;

extern void gdt_flush(gdt_ptr_t* ptr);
extern void tss_flush(void);

static void _gdt_encode(uint32_t index, uint32_t base, uint32_t limit,
                        uint8_t access, uint8_t flags) {
    g_gdt[index].limit_low   = (uint16_t)(limit & 0xFFFF);
    g_gdt[index].base_low    = (uint16_t)(base & 0xFFFF);
    g_gdt[index].base_mid    = (uint8_t)((base >> 16) & 0xFF);
    g_gdt[index].access      = access;
    g_gdt[index].granularity = ((flags & 0x0F) << 4) | ((limit >> 16) & 0x0F);
    g_gdt[index].base_high   = (uint8_t)((base >> 24) & 0xFF);
}

os_status_t gdt_init(void) {
    _gdt_encode(GDT_NULL, 0, 0, 0, 0);

    _gdt_encode(GDT_KERNEL_CS, 0, 0xFFFFF,
        GDT_PRESENT | GDT_DPL0 | GDT_CODE_DATA | GDT_EXEC | GDT_RW,
        GDT_GRAN_4K | GDT_SIZE_32);

    _gdt_encode(GDT_KERNEL_DS, 0, 0xFFFFF,
        GDT_PRESENT | GDT_DPL0 | GDT_CODE_DATA | GDT_RW,
        GDT_GRAN_4K | GDT_SIZE_32);

    _gdt_encode(GDT_USER_CS, 0, 0xFFFFF,
        GDT_PRESENT | GDT_DPL3 | GDT_CODE_DATA | GDT_EXEC | GDT_RW,
        GDT_GRAN_4K | GDT_SIZE_32);

    _gdt_encode(GDT_USER_DS, 0, 0xFFFFF,
        GDT_PRESENT | GDT_DPL3 | GDT_CODE_DATA | GDT_RW,
        GDT_GRAN_4K | GDT_SIZE_32);

    /* TSS: system descriptor, base = &g_tss, limit = sizeof(tss_t)-1 */
    _gdt_encode(GDT_TSS, (uint32_t)&g_tss, sizeof(tss_t) - 1,
        GDT_PRESENT | GDT_DPL0 | GDT_TSS_AVAIL,
        GDT_GRAN_1B | GDT_SIZE_16);

    g_gdt_ptr.limit = (uint16_t)(sizeof(g_gdt) - 1);
    g_gdt_ptr.base  = (uint32_t)&g_gdt;
    gdt_flush(&g_gdt_ptr);

    /* Initialize TSS — only esp0/ss0 matter for now */
    g_tss.esp0 = 0;
    g_tss.ss0  = SELECTOR(GDT_KERNEL_DS, 0);

    /* Load task register */
    tss_flush();

    return OS_OK;
}

void gdt_set_kernel_stack(uint32_t esp) {
    g_tss.esp0 = esp;
}
