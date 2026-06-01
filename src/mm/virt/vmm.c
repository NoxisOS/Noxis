/**
 * @file    mm/vmm.c
 * @brief   Virtual Memory Manager — page directory and table management
 * @author  Noxis Team
 * @date    2026-05-29
 */
#include <mm/virt/vmm.h>
#include <mm/virt/paging.h>
#include <mm/phys/pmm.h>
#include <common/types.h>

/* ── constants ──────────────────────────────────────────────── */

/* Physical address of the kernel's own page directory.
   Set by the boot stub; PD@0x400000, PT0@0x401000, PTK@0x402000. */
#define KERNEL_PD_PHYS  0x400000u

/* Two kernel-space scratch virtual addresses used to temporarily map
   arbitrary physical frames for reading/writing.  They live in the
   kernel-heap region so they are present in every PD. */
#define VMM_SCRATCH_0   0xD0FE0000u
#define VMM_SCRATCH_1   0xD0FE1000u

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

/* ── per-process address space ─────────────────────────────── */

uint32_t vmm_get_pd_phys(void) {
    uint32_t cr3;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
    return cr3 & ~0xFFFu;
}

/* Map one physical frame at a scratch VA inside the CURRENT PD.
   Returns a pointer to the scratch window. */
static uint32_t* _scratch(uint32_t va, uint32_t phys) {
    uint32_t* pd  = (uint32_t*)RECURSIVE_VADDR;
    uint32_t pde  = PDE_INDEX(va);
    uint32_t pte  = PTE_INDEX(va);

    /* Ensure the scratch PT exists (it will after the first call). */
    if (!(pd[pde] & PAGE_PRESENT)) {
        uint32_t pt_phys;
        if (pmm_alloc_frame(&pt_phys) != OS_OK) return (uint32_t*)0;
        pd[pde] = pt_phys | PAGE_PRESENT | PAGE_RW;
        uint32_t* pt = (uint32_t*)(0xFFC00000u + pde * PAGE_SIZE);
        for (uint32_t i = 0; i < 1024; i++) pt[i] = 0;
    }
    uint32_t* pt = (uint32_t*)(0xFFC00000u + pde * PAGE_SIZE);
    pt[pte] = (phys & ~0xFFFu) | PAGE_PRESENT | PAGE_RW;
    vmm_invlpg(va);
    return (uint32_t*)va;
}

os_status_t vmm_create_pd(uint32_t* pd_phys_out) {
    uint32_t phys;
    if (pmm_alloc_frame(&phys) != OS_OK) return OS_ERR_OOM;

    /* Zero the new PD through the scratch window. */
    uint32_t* new_pd = _scratch(VMM_SCRATCH_0, phys);
    if (!new_pd) { pmm_free_frame(phys); return OS_ERR_OOM; }
    for (uint32_t i = 0; i < 1024; i++) new_pd[i] = 0;

    /* Clone from the current PD (= kernel PD when called from exec_run). */
    uint32_t* kpd = (uint32_t*)RECURSIVE_VADDR;

    /* PDE 0 — identity map for 0x00000000..0x003FFFFF.
       Contains the VGA buffer (0xB8000), BIOS, and other low-memory regions
       that the kernel must reach even when a per-process PD is active. */
    new_pd[0] = kpd[0];

    /* PDEs 768..1022 — higher-half kernel (0xC0000000+): code, data, heap,
       kernel stacks, PIC/PIT MMIO, recursive mapping window, etc. */
    for (uint32_t i = 768; i < 1023; i++) new_pd[i] = kpd[i];

    /* Recursive self-reference in the new PD. */
    new_pd[1023] = phys | PAGE_PRESENT | PAGE_RW;

    vmm_invlpg(VMM_SCRATCH_0);
    *pd_phys_out = phys;
    return OS_OK;
}

os_status_t vmm_map_page_in(uint32_t pd_phys, uint32_t virt,
                              uint32_t phys, uint32_t flags) {
    uint32_t pde_idx = PDE_INDEX(virt);
    uint32_t pte_idx = PTE_INDEX(virt);

    /* Read/update the target PD via scratch0. */
    uint32_t* pd = _scratch(VMM_SCRATCH_0, pd_phys);
    if (!pd) return OS_ERR_OOM;

    if (!(pd[pde_idx] & PAGE_PRESENT)) {
        /* Allocate a new page table and zero it through scratch1. */
        uint32_t pt_phys;
        if (pmm_alloc_frame(&pt_phys) != OS_OK) return OS_ERR_OOM;
        uint32_t* pt1 = _scratch(VMM_SCRATCH_1, pt_phys);
        if (!pt1) { pmm_free_frame(pt_phys); return OS_ERR_OOM; }
        for (uint32_t i = 0; i < 1024; i++) pt1[i] = 0;
        vmm_invlpg(VMM_SCRATCH_1);

        /* Re-map scratch0 to the PD (scratch1 may have allocated a PT for
           VMM_SCRATCH_1 which shares the same PDE as VMM_SCRATCH_0). */
        pd = _scratch(VMM_SCRATCH_0, pd_phys);
        if (!pd) return OS_ERR_OOM;
        pd[pde_idx] = pt_phys | PAGE_PRESENT | PAGE_RW | (flags & PAGE_USER);
        vmm_invlpg(VMM_SCRATCH_0);
    }

    uint32_t pt_phys = pd[pde_idx] & ~0xFFFu;
    vmm_invlpg(VMM_SCRATCH_0);

    uint32_t* pt = _scratch(VMM_SCRATCH_1, pt_phys);
    if (!pt) return OS_ERR_OOM;
    pt[pte_idx] = (phys & ~0xFFFu) | (flags & 0xFFFu) | PAGE_PRESENT;
    vmm_invlpg(VMM_SCRATCH_1);
    return OS_OK;
}

uint32_t vmm_virt_to_phys_in(uint32_t pd_phys, uint32_t virt) {
    uint32_t* pd = _scratch(VMM_SCRATCH_0, pd_phys);
    if (!pd) return 0;
    uint32_t pde = pd[PDE_INDEX(virt)];
    vmm_invlpg(VMM_SCRATCH_0);
    if (!(pde & PAGE_PRESENT)) return 0;

    uint32_t* pt = _scratch(VMM_SCRATCH_1, pde & ~0xFFFu);
    if (!pt) return 0;
    uint32_t pte = pt[PTE_INDEX(virt)];
    vmm_invlpg(VMM_SCRATCH_1);
    if (!(pte & PAGE_PRESENT)) return 0;
    return (pte & ~0xFFFu) | PAGE_OFFSET(virt);
}

/* ── vmm_unmap_page_in ────────────────────────────────────────
   Clear the PTE for `virt` in the target PD and free the backing
   physical frame.  No-op if the page is not present.  Returns the
   freed physical frame (page-aligned), or 0 if nothing was mapped. */
uint32_t vmm_unmap_page_in(uint32_t pd_phys, uint32_t virt) {
    uint32_t* pd = _scratch(VMM_SCRATCH_0, pd_phys);
    if (!pd) return 0;
    uint32_t pde = pd[PDE_INDEX(virt)];
    vmm_invlpg(VMM_SCRATCH_0);
    if (!(pde & PAGE_PRESENT)) return 0;

    uint32_t* pt = _scratch(VMM_SCRATCH_1, pde & ~0xFFFu);
    if (!pt) return 0;
    uint32_t pte = pt[PTE_INDEX(virt)];
    if (!(pte & PAGE_PRESENT)) { vmm_invlpg(VMM_SCRATCH_1); return 0; }

    uint32_t frame = pte & ~0xFFFu;
    pt[PTE_INDEX(virt)] = 0;            /* clear the mapping */
    vmm_invlpg(VMM_SCRATCH_1);

    pmm_free_frame(frame);             /* reclaim the physical page */
    vmm_invlpg(virt);                  /* flush any stale TLB entry  */
    return frame;
}

/* ── vmm_handle_cow ───────────────────────────────────────────
   Resolve a copy-on-write fault at `fault_addr` in the CURRENT PD.
   Returns 1 if the fault was a genuine CoW page and was resolved
   (caller should retry the instruction), 0 otherwise (not a CoW
   page, or out of memory → caller treats it as a real fault).     */
int vmm_handle_cow(uint32_t fault_addr) {
    uint32_t va    = PAGE_ALIGN_DOWN(fault_addr);
    uint32_t pde_i = PDE_INDEX(va);
    uint32_t pte_i = PTE_INDEX(va);

    uint32_t* pd = (uint32_t*)RECURSIVE_VADDR;
    if (!(pd[pde_i] & PAGE_PRESENT)) return 0;

    uint32_t* pt  = (uint32_t*)(0xFFC00000u + pde_i * PAGE_SIZE);
    uint32_t  pte = pt[pte_i];
    if (!(pte & PAGE_PRESENT) || !(pte & PAGE_COW)) return 0;

    uint32_t old_frame = pte & ~0xFFFu;
    uint32_t flags     = ((pte & 0xFFFu) & ~PAGE_COW) | PAGE_RW;

    /* Sole owner → no copy needed, just restore write permission. */
    if (pmm_ref_count(old_frame) <= 1) {
        pt[pte_i] = old_frame | flags;
        vmm_invlpg(va);
        return 1;
    }

    /* Shared → allocate a private copy. */
    uint32_t new_frame;
    if (pmm_alloc_frame(&new_frame) != OS_OK) return 0;

    /* Copy old → new.  old_frame is still readable at `va`;
       new_frame is mapped through the scratch window.            */
    uint8_t* dst = (uint8_t*)_scratch(VMM_SCRATCH_1, new_frame);
    if (!dst) { pmm_free_frame(new_frame); return 0; }
    uint8_t* src = (uint8_t*)va;
    for (uint32_t b = 0; b < PAGE_SIZE; b++) dst[b] = src[b];
    vmm_invlpg(VMM_SCRATCH_1);

    /* Point this process at its private, writable copy. */
    pt[pte_i] = new_frame | flags;
    vmm_invlpg(va);

    /* Drop our reference to the formerly-shared frame. */
    pmm_free_frame(old_frame);
    return 1;
}

os_status_t vmm_fork_pd(uint32_t parent_pd_phys, uint32_t* child_pd_out) {
    uint32_t child_phys;
    os_status_t s = vmm_create_pd(&child_phys);
    if (s != OS_OK) return s;

    /* Walk user-space PDEs 1..767 (virt 0x00400000..0xBFFFFFFF).
       PDE 0 (0x0..0x3FFFFF) is the identity mapping shared across all
       address spaces — VGA, BIOS, etc.  Copying it would privatise the
       VGA buffer and waste 4 MB of physical frames. */
    for (uint32_t pde_idx = 1; pde_idx < 768; pde_idx++) {
        /* Read parent PDE. */
        uint32_t* parent_pd = _scratch(VMM_SCRATCH_0, parent_pd_phys);
        if (!parent_pd) goto fail;
        uint32_t pde = parent_pd[pde_idx];
        vmm_invlpg(VMM_SCRATCH_0);
        if (!(pde & PAGE_PRESENT)) continue;

        uint32_t parent_pt_phys = pde & ~0xFFFu;

        /* Walk all PTEs in this page table. */
        for (uint32_t pte_idx = 0; pte_idx < 1024; pte_idx++) {
            /* Read parent PTE. */
            uint32_t* parent_pt = _scratch(VMM_SCRATCH_0, parent_pt_phys);
            if (!parent_pt) goto fail;
            uint32_t pte = parent_pt[pte_idx];
            vmm_invlpg(VMM_SCRATCH_0);
            if (!(pte & PAGE_PRESENT)) continue;
            /* Only copy pages accessible from ring 3 (PAGE_USER).
               Supervisor-only pages are shared across all PDs. */
            if (!(pte & PAGE_USER)) continue;

            uint32_t src_phys  = pte & ~0xFFFu;
            uint32_t pte_flags = pte &  0xFFFu;
            uint32_t virt      = (pde_idx << 22) | (pte_idx << 12);

            /* ── Copy-on-write share ───────────────────────────
               Instead of duplicating the page, parent and child
               share the SAME frame.  Writable pages are remapped
               read-only with PAGE_COW set in BOTH page tables; the
               first write in either process faults and triggers a
               private copy (see pagefault.c).  Read-only pages are
               shared as-is (a write to them is a genuine fault).  */
            uint32_t child_flags = pte_flags;

            if (pte_flags & PAGE_RW) {
                /* Make this a CoW page: drop RW, mark COW. */
                child_flags = (pte_flags & ~PAGE_RW) | PAGE_COW;

                /* Rewrite the PARENT's PTE through the scratch window
                   so the parent also faults on its next write.        */
                uint32_t* ppt = _scratch(VMM_SCRATCH_0, parent_pt_phys);
                if (!ppt) goto fail;
                ppt[pte_idx] = src_phys | child_flags;
                vmm_invlpg(VMM_SCRATCH_0);
                vmm_invlpg(virt);   /* parent IS the current PD here */
            }

            /* The child now references the shared frame too. */
            pmm_ref_inc(src_phys);

            /* Map the child at the same virtual address, same frame. */
            s = vmm_map_page_in(child_phys, virt, src_phys, child_flags);
            if (s != OS_OK) { pmm_free_frame(src_phys); goto fail; }
        }
    }

    *child_pd_out = child_phys;
    return OS_OK;

fail:
    vmm_destroy_pd(child_phys);
    return OS_ERR_OOM;
}

void vmm_switch_pd(uint32_t pd_phys) {
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(pd_phys) : "memory");
}

void vmm_destroy_pd(uint32_t pd_phys) {
    /* Walk user PDEs 1..767, free page frames and PT frames.
       PDE 0 is the identity map (PT0 @ 0x401000) SHARED between every
       address space — vmm_create_pd copies the PDE value, not the PT.
       Freeing its frames would release kernel heap memory and corrupt PT0. */
    for (uint32_t pde_idx = 1; pde_idx < 768; pde_idx++) {
        uint32_t* pd = _scratch(VMM_SCRATCH_0, pd_phys);
        if (!pd) return;
        uint32_t pde = pd[pde_idx];
        vmm_invlpg(VMM_SCRATCH_0);
        if (!(pde & PAGE_PRESENT)) continue;

        uint32_t pt_phys = pde & ~0xFFFu;
        uint32_t* pt = _scratch(VMM_SCRATCH_1, pt_phys);
        if (pt) {
            for (uint32_t pte_idx = 0; pte_idx < 1024; pte_idx++) {
                if (pt[pte_idx] & PAGE_PRESENT)
                    pmm_free_frame(pt[pte_idx] & ~0xFFFu);
            }
            vmm_invlpg(VMM_SCRATCH_1);
        }
        pmm_free_frame(pt_phys);
    }
    pmm_free_frame(pd_phys);
}
