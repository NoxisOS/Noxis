/**
 * @file    mm/virt/vmm.h
 * @brief   Virtual memory manager — 4-level paging (x86-64).
 * @author  Noxis Team
 */
#ifndef MM_VMM_H
#define MM_VMM_H

#include <common/types.h>
#include <common/status.h>

#define PHYSMAP_BASE  0xFFFF800000000000ULL  /* all RAM mapped here */

#define PAGE_PRESENT  0x1
#define PAGE_RW       0x2
#define PAGE_USER     0x4
#define PAGE_COW      0x200  /* AVL bit — software copy-on-write marker */

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

/* Deep-copy the user half (PML4[0]) of src_pml4 into dst_pml4. Returns 0 ok. */
int vmm_copy_user_space(uint64_t dst_pml4, uint64_t src_pml4);

/* CoW fork: mark all src user pages read-only + CoW, share frames into dst.
   Both processes fault-copy on the first write.  Returns 0 on success. */
int vmm_cow_user_space(uint64_t dst_pml4, uint64_t src_pml4);

/* Walk PML4[0] and release every user page (respecting refcounts) plus all
   intermediate page-table frames.  Safe to call after vmm_switch away. */
void vmm_free_user_space(uint64_t pml4_phys);

/* ISR vector-14 (#PF) handler — register with isr_register_handler(14, ...).
   Resolves CoW faults; panics on genuine protection violations. */
#include <kernel/isr/isr.h>
void vmm_page_fault_handler(isr_frame_t* frame);

#endif /* MM_VMM_H */
