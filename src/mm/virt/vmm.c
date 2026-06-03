/**
 * @file    mm/virt/vmm.c
 * @brief   Virtual memory manager — 4-level paging (PML4/PDPT/PD/PT).
 */
#include <mm/virt/vmm.h>
#include <mm/phys/pmm.h>
#include <drivers/serial.h>

void serial_write_hex64(uint64_t v);

#define PAGE_PS       0x80
#define ENTRIES       512
#define MAP_2M        (2ULL * 1024 * 1024)
#define IDENTITY_MB   128

static uint64_t* g_pml4;

static uint64_t* as_table(uint64_t phys) { return (uint64_t*)phys; }
static void zero_table(uint64_t* t) { for (int i = 0; i < ENTRIES; i++) t[i] = 0; }

os_status_t vmm_init(void) {
    g_pml4 = as_table(pmm_alloc_frame());
    zero_table(g_pml4);

    uint64_t* pdpt = as_table(pmm_alloc_frame());
    zero_table(pdpt);
    g_pml4[0] = (uint64_t)pdpt | PAGE_PRESENT | PAGE_RW;

    uint64_t* pd = as_table(pmm_alloc_frame());
    zero_table(pd);
    pdpt[0] = (uint64_t)pd | PAGE_PRESENT | PAGE_RW;

    uint64_t pages = (IDENTITY_MB * 1024ULL * 1024ULL) / MAP_2M;
    for (uint64_t i = 0; i < pages; i++)
        pd[i] = (i * MAP_2M) | PAGE_PRESENT | PAGE_RW | PAGE_PS;

    __asm__ __volatile__("mov %0, %%cr3" :: "r"((uint64_t)g_pml4) : "memory");

    serial_write((const uint8_t*)"[noxis64] VMM PML4="); serial_write_hex64((uint64_t)g_pml4);
    serial_write((const uint8_t*)"\n");
    return OS_OK;
}

int vmm_map_page(uint64_t va, uint64_t pa, uint64_t flags) {
    uint64_t i4 = (va >> 39) & 0x1FF, i3 = (va >> 30) & 0x1FF;
    uint64_t i2 = (va >> 21) & 0x1FF, i1 = (va >> 12) & 0x1FF;

    uint64_t u = flags & PAGE_USER;   /* propagate USER down every level */

    uint64_t* pml4 = g_pml4;
    if (!(pml4[i4] & PAGE_PRESENT)) {
        uint64_t t = pmm_alloc_frame(); if (!t) return -1;
        zero_table(as_table(t));
        pml4[i4] = t | PAGE_PRESENT | PAGE_RW | u;
    } else { pml4[i4] |= u; }
    uint64_t* pdpt = as_table(pml4[i4] & ~0xFFFULL);
    if (!(pdpt[i3] & PAGE_PRESENT)) {
        uint64_t t = pmm_alloc_frame(); if (!t) return -1;
        zero_table(as_table(t));
        pdpt[i3] = t | PAGE_PRESENT | PAGE_RW | u;
    } else { pdpt[i3] |= u; }
    uint64_t* pd = as_table(pdpt[i3] & ~0xFFFULL);
    if (!(pd[i2] & PAGE_PRESENT)) {
        uint64_t t = pmm_alloc_frame(); if (!t) return -1;
        zero_table(as_table(t));
        pd[i2] = t | PAGE_PRESENT | PAGE_RW | u;
    } else { pd[i2] |= u; }
    uint64_t* pt = as_table(pd[i2] & ~0xFFFULL);
    pt[i1] = (pa & ~0xFFFULL) | (flags & 0xFFF) | PAGE_PRESENT;
    __asm__ __volatile__("invlpg (%0)" :: "r"(va) : "memory");
    return 0;
}
