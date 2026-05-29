/**
 * @file    mm/vmm.h
 * @brief   Virtual Memory Manager interface
 * @author  Noxis Team
 * @date    2026-05-29
 */
#ifndef MM_VMM_H
#define MM_VMM_H

#include <common/types.h>
#include <common/status.h>

/**
 * @brief Initializes paging and higher-half kernel mapping.
 *        Must be called from ASM before any C code at higher-half.
 * @param pd_phys   Physical address of the page directory
 * @param pt0_phys  Physical address of identity page table (0-4MB)
 * @param pt_kernel_phys  Physical address of kernel page table
 * @return OS_OK on success
 */
os_status_t vmm_init(uint32_t pd_phys, uint32_t pt0_phys, uint32_t pt_kernel_phys);

/**
 * @brief Maps a 4 KB virtual page to a physical frame
 * @param virt   Virtual address (page-aligned)
 * @param phys   Physical address (page-aligned)
 * @param flags  Page flags (PAGE_PRESENT | PAGE_RW, etc.)
 * @return OS_OK on success
 */
os_status_t vmm_map_page(uint32_t virt, uint32_t phys, uint32_t flags);

/**
 * @brief Invalidates a single TLB entry
 * @param virt  Virtual address to invalidate
 */
void vmm_invlpg(uint32_t virt);

#endif /* MM_VMM_H */
