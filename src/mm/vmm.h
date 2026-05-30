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

/* ── per-process address space ─────────────────────────────── */

/** Returns the physical address of the currently-active page directory. */
uint32_t vmm_get_pd_phys(void);

/**
 * @brief Allocates a new page directory.
 *        Kernel PDEs (0xC0000000+) are cloned from the active kernel PD;
 *        user PDEs (< 0xC0000000) are zeroed.
 * @param pd_phys_out  Receives the physical address of the new PD.
 */
os_status_t vmm_create_pd(uint32_t* pd_phys_out);

/**
 * @brief Maps virt→phys inside a specific (possibly non-current) PD.
 *        Uses two scratch virtual pages to access the target PD/PT.
 */
os_status_t vmm_map_page_in(uint32_t pd_phys, uint32_t virt,
                             uint32_t phys, uint32_t flags);

/**
 * @brief Resolves virt to a physical address inside a specific PD.
 * @return physical address, or 0 if not mapped.
 */
uint32_t vmm_virt_to_phys_in(uint32_t pd_phys, uint32_t virt);

/**
 * @brief Deep-copies all user-space pages (virt < 0xC0000000) from
 *        parent's PD into a freshly-created child PD.
 * @param parent_pd_phys  Parent's page directory physical address.
 * @param child_pd_out    Receives the child's PD physical address.
 */
os_status_t vmm_fork_pd(uint32_t parent_pd_phys, uint32_t* child_pd_out);

/** Loads a page directory into CR3 (switches active address space). */
void vmm_switch_pd(uint32_t pd_phys);

/**
 * @brief Frees all user-space frames+tables inside pd_phys, then frees
 *        the PD frame itself.  Kernel PDEs are left untouched.
 */
void vmm_destroy_pd(uint32_t pd_phys);

#endif /* MM_VMM_H */
