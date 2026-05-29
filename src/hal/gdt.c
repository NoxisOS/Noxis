/**
 * @file    hal/gdt.c
 * @brief   Global Descriptor Table — set up segments and TSS
 * @author  Noxis Team
 * @date    2026-05-29
 */
#include <hal/gdt.h>
#include <common/types.h>

/* ── file-scope state ──────────────────────────────────────── */
static gdt_entry_t g_gdt[GDT_ENTRIES];
static gdt_ptr_t   g_gdt_ptr;

/* ── external ASM ──────────────────────────────────────────── */
extern void gdt_flush(gdt_ptr_t* ptr);

/* ── private functions ─────────────────────────────────────── */

/**
 * @brief Encodes a GDT entry from its components
 */
static void _gdt_encode(uint32_t index,
                        uint32_t base, uint32_t limit,
                        uint8_t access, uint8_t flags) {
    g_gdt[index].limit_low   = (uint16_t)(limit & 0xFFFF);
    g_gdt[index].base_low    = (uint16_t)(base & 0xFFFF);
    g_gdt[index].base_mid    = (uint8_t)((base >> 16) & 0xFF);
    g_gdt[index].access      = access;
    g_gdt[index].granularity = ((flags & 0x0F) << 4) | ((limit >> 16) & 0x0F);
    g_gdt[index].base_high   = (uint8_t)((base >> 24) & 0xFF);
}

/* ── public functions ──────────────────────────────────────── */

os_status_t gdt_init(void) {
    /* Null descriptor — required by Intel */
    _gdt_encode(GDT_NULL, 0, 0, 0, 0);

    /* Kernel code: ring 0, execute/read, 32-bit, 4 GB */
    _gdt_encode(GDT_KERNEL_CS, 0, 0xFFFFF,
                GDT_PRESENT | GDT_DPL0 | GDT_CODE_DATA | GDT_EXEC | GDT_RW,
                GDT_GRAN_4K | GDT_SIZE_32);

    /* Kernel data: ring 0, read/write, 32-bit, 4 GB */
    _gdt_encode(GDT_KERNEL_DS, 0, 0xFFFFF,
                GDT_PRESENT | GDT_DPL0 | GDT_CODE_DATA | GDT_RW,
                GDT_GRAN_4K | GDT_SIZE_32);

    /* User code: ring 3, execute/read, 32-bit, 4 GB */
    _gdt_encode(GDT_USER_CS, 0, 0xFFFFF,
                GDT_PRESENT | GDT_DPL3 | GDT_CODE_DATA | GDT_EXEC | GDT_RW,
                GDT_GRAN_4K | GDT_SIZE_32);

    /* User data: ring 3, read/write, 32-bit, 4 GB */
    _gdt_encode(GDT_USER_DS, 0, 0xFFFFF,
                GDT_PRESENT | GDT_DPL3 | GDT_CODE_DATA | GDT_RW,
                GDT_GRAN_4K | GDT_SIZE_32);

    /* TSS placeholder — base and limit set when TSS is allocated */
    _gdt_encode(GDT_TSS, 0, 0, 0, 0);

    /* Load the new GDT */
    g_gdt_ptr.limit = (uint16_t)(sizeof(g_gdt) - 1);
    g_gdt_ptr.base  = (uint32_t)&g_gdt;
    gdt_flush(&g_gdt_ptr);

    return OS_OK;
}
