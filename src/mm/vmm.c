/**
 * @file    mm/vmm.c
 * @brief   Virtual Memory Manager — page directory and table management
 * @author  Noxis Team
 * @date    2026-05-29
 */
#include <mm/vmm.h>
#include <mm/paging.h>
#include <mm/pmm.h>
#include <common/types.h>

/* ── public functions ──────────────────────────────────────── */

os_status_t vmm_init(uint32_t pd_phys, uint32_t pt0_phys, uint32_t pt_kernel_phys) {
    uint32_t* pd = (uint32_t*)pd_phys;
    uint32_t* pt0 = (uint32_t*)pt0_phys;
    uint32_t* ptk = (uint32_t*)pt_kernel_phys;

    /* ── CRITICAL: do NOT access globals here ─────────────────
       This function runs BEFORE paging. Global variables are
       linked at 0xC01xxxxx which is not yet mapped. Only local
       variables and parameters (on the stack at physical < 4MB)
       are safe to use.
    ─────────────────────────────────────────────────────────── */

    /* Zero the page directory and both page tables */
    for (uint32_t i = 0; i < 1024; i++) {
        pd[i]  = 0;
        pt0[i] = 0;
        ptk[i] = 0;
    }

    /* Identity map first 4 MB (0x00000000 → 0x00000000, page by page) */
    for (uint32_t i = 0; i < 1024; i++) {
        pt0[i] = (i * PAGE_SIZE) | PAGE_PRESENT | PAGE_RW;
    }

    /* Map kernel at 0xC0000000 → physical 0x00000000 (4 MB, all pages RW) */
    /* This maps 0xC0100000 (kernel link address) → physical 0x100000 */
    for (uint32_t i = 0; i < 1024; i++) {
        ptk[i] = (i * PAGE_SIZE) | PAGE_PRESENT | PAGE_RW;
    }

    /* Set PDE 0 → identity page table */
    pd[0] = pt0_phys | PAGE_PRESENT | PAGE_RW;

    /* Set PDE for kernel (index 768 = 0xC0000000 / 4MB) → kernel page table */
    pd[PDE_INDEX(KERNEL_VIRT_BASE)] = pt_kernel_phys | PAGE_PRESENT | PAGE_RW;

    /* Recursive mapping: last PDE → page directory itself */
    pd[RECURSIVE_INDEX] = pd_phys | PAGE_PRESENT | PAGE_RW;

    /* Store PD physical address for later use (safe now — paging not yet enabled,
       but the write to g_pd_phys goes to physical 0xC01xxxxx which is NOT mapped.
       We defer this to post-paging init. */
    (void)pd_phys; /* unused for now */

    return OS_OK;
}

os_status_t vmm_map_page(uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t* pd  = (uint32_t*)RECURSIVE_VADDR;
    uint32_t pde_idx = PDE_INDEX(virt);
    uint32_t pte_idx = PTE_INDEX(virt);

    /* Check if page table exists */
    if (!(pd[pde_idx] & PAGE_PRESENT)) {
        uint32_t pt_phys;
        if (pmm_alloc_frame(&pt_phys) != OS_OK) return OS_ERR_OOM;
        pd[pde_idx] = pt_phys | PAGE_PRESENT | PAGE_RW | (flags & PAGE_USER);

        /* Zero the new page table via recursive mapping */
        uint32_t* pt = (uint32_t*)(0xFFC00000 + pde_idx * PAGE_SIZE);
        for (uint32_t i = 0; i < 1024; i++) {
            pt[i] = 0;
        }
    }

    /* Map the page via recursive mapping */
    uint32_t* pt = (uint32_t*)(0xFFC00000 + pde_idx * PAGE_SIZE);
    pt[pte_idx] = PAGE_ALIGN_DOWN(phys) | (flags & 0xFFF) | PAGE_PRESENT;

    vmm_invlpg(virt);
    return OS_OK;
}
