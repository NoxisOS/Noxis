/**
 * @file    mm/virt/vmm.h
 * @brief   Virtual memory manager — 4-level paging (x86-64).
 * @author  Noxis Team
 */
#ifndef MM_VMM_H
#define MM_VMM_H

#include <common/types.h>
#include <common/status.h>

#define PAGE_PRESENT  0x1
#define PAGE_RW       0x2
#define PAGE_USER     0x4

/* Build the kernel PML4 (low identity + physmap + higher-half) and load it. */
os_status_t vmm_init(void);

/* Map one 4 KB page va → pa in the current (kernel) address space. */
int vmm_map_page(uint64_t va, uint64_t pa, uint64_t flags);

/* Map one 4 KB page into a specific PML4 (physical address). */
int vmm_map_page_into(uint64_t pml4_phys, uint64_t va, uint64_t pa, uint64_t flags);

/* Physical address of the kernel PML4. */
uint64_t vmm_kernel_pml4(void);

/* Load a PML4 into CR3 (switch address space). */
void vmm_switch(uint64_t pml4_phys);

/* Create a new address space (private user half, shared kernel + physmap).
   Returns the new PML4 physical address, or 0 on failure. */
uint64_t vmm_create_address_space(void);

#endif /* MM_VMM_H */
