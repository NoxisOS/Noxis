/**
 * @file    mm/virt/vmm.c
 * @brief   Virtual memory manager — 4-level paging, physmap, address spaces.
 *
 * Address-space layout (per process PML4):
 *   PML4[0]   — private user half (0 .. 0x7FFFFFFFFFFF)
 *   PML4[256] — physmap: all RAM mapped at 0xFFFF800000000000 (shared)
 *   PML4[511] — kernel image at 0xFFFFFFFF80000000 (shared)
 *
 * The kernel accesses physical memory through the physmap, so PML4[0]
 * can be fully private to each user process.
 */
#include <mm/virt/vmm.h>
#include <mm/virt/uvm.h>
#include <mm/phys/pmm.h>
#include <drivers/serial.h>
#include <kernel/isr/isr.h>
#include <proc/scheduler.h>

void serial_write_hex64(uint64_t v);

#define PAGE_PS        0x80
#define ENTRIES        512
#define MAP_2M         (2ULL * 1024 * 1024)
#define IDENTITY_MB    128
#define PML4_PHYSMAP   256
#define PML4_KERNEL    511

static uint64_t  g_kernel_pml4;     /* physical address of the kernel PML4 */
static int       g_physmap_on = 0;  /* 0 = low identity, 1 = use physmap   */

/* Map a physical address to a kernel-accessible pointer. Before the physmap
   is live we rely on the boot's low identity map (phys == virt). */
static uint64_t* as_table(uint64_t phys) {
    return g_physmap_on ? (uint64_t*)(PHYSMAP_BASE + phys) : (uint64_t*)phys;
}
static void zero_table(uint64_t* t) { for (int i = 0; i < ENTRIES; i++) t[i] = 0; }

os_status_t vmm_init(void) {
    uint64_t pml4_phys = pmm_alloc_frame();
    uint64_t* pml4 = as_table(pml4_phys);
    zero_table(pml4);

    /* Low identity (0..128 MB) — used during the CR3 switch and kept so the
       kernel address space can still reach low RAM directly. */
    uint64_t pdpt_phys = pmm_alloc_frame();
    uint64_t* pdpt = as_table(pdpt_phys);
    zero_table(pdpt);
    pml4[0] = pdpt_phys | PAGE_PRESENT | PAGE_RW;

    uint64_t pd_phys = pmm_alloc_frame();
    uint64_t* pd = as_table(pd_phys);
    zero_table(pd);
    pdpt[0] = pd_phys | PAGE_PRESENT | PAGE_RW;
    for (uint64_t i = 0; i < (IDENTITY_MB * 1024ULL * 1024ULL) / MAP_2M; i++)
        pd[i] = (i * MAP_2M) | PAGE_PRESENT | PAGE_RW | PAGE_PS;

    /* Physmap: PML4[256] → PDPT → PD (1 GB of RAM via 2 MB pages). */
    uint64_t pmpdpt_phys = pmm_alloc_frame();
    uint64_t* pmpdpt = as_table(pmpdpt_phys);
    zero_table(pmpdpt);
    pml4[PML4_PHYSMAP] = pmpdpt_phys | PAGE_PRESENT | PAGE_RW;

    uint64_t pmpd_phys = pmm_alloc_frame();
    uint64_t* pmpd = as_table(pmpd_phys);
    zero_table(pmpd);
    pmpdpt[0] = pmpd_phys | PAGE_PRESENT | PAGE_RW;
    for (uint64_t i = 0; i < ENTRIES; i++)
        pmpd[i] = (i * MAP_2M) | PAGE_PRESENT | PAGE_RW | PAGE_PS;

    /* Higher-half kernel mapping from the boot PML4 (at phys 0x1000). */
    uint64_t* boot_pml4 = (uint64_t*)0x1000;
    pml4[PML4_KERNEL] = boot_pml4[PML4_KERNEL];

    g_kernel_pml4 = pml4_phys;
    __asm__ __volatile__("mov %0, %%cr3" :: "r"(pml4_phys) : "memory");
    g_physmap_on = 1;   /* from now on as_table() uses the physmap */

    serial_write((const uint8_t*)"[noxis64] VMM PML4="); serial_write_hex64(pml4_phys);
    serial_write((const uint8_t*)" physmap=on\n");
    return OS_OK;
}

uint64_t vmm_kernel_pml4(void) { return g_kernel_pml4; }

void vmm_switch(uint64_t pml4_phys) {
    __asm__ __volatile__("mov %0, %%cr3" :: "r"(pml4_phys) : "memory");
}

/* Map one 4 KB page into the given PML4 (physical), allocating tables. */
int vmm_map_page_into(uint64_t pml4_phys, uint64_t va, uint64_t pa, uint64_t flags) {
    uint64_t i4 = (va >> 39) & 0x1FF, i3 = (va >> 30) & 0x1FF;
    uint64_t i2 = (va >> 21) & 0x1FF, i1 = (va >> 12) & 0x1FF;
    uint64_t u = flags & PAGE_USER;

    uint64_t* pml4 = as_table(pml4_phys);
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

/* Map into the current (kernel) address space. */
int vmm_map_page(uint64_t va, uint64_t pa, uint64_t flags) {
    return vmm_map_page_into(g_kernel_pml4, va, pa, flags);
}

/* Copy the entire user half (PML4[0] subtree) of src into dst, allocating
   fresh frames and duplicating page contents through the physmap. Intermediate
   tables are recreated by vmm_map_page_into. Returns 0 on success. */
int vmm_copy_user_space(uint64_t dst_pml4, uint64_t src_pml4) {
    uint64_t* spml4 = as_table(src_pml4);
    if (!(spml4[0] & PAGE_PRESENT)) return 0;        /* nothing mapped */
    uint64_t* spdpt = as_table(spml4[0] & ~0xFFFULL);

    for (uint64_t i3 = 0; i3 < ENTRIES; i3++) {
        if (!(spdpt[i3] & PAGE_PRESENT)) continue;
        uint64_t* spd = as_table(spdpt[i3] & ~0xFFFULL);
        for (uint64_t i2 = 0; i2 < ENTRIES; i2++) {
            if (!(spd[i2] & PAGE_PRESENT) || (spd[i2] & PAGE_PS)) continue;
            uint64_t* spt = as_table(spd[i2] & ~0xFFFULL);
            for (uint64_t i1 = 0; i1 < ENTRIES; i1++) {
                if (!(spt[i1] & PAGE_PRESENT)) continue;
                uint64_t va    = (i3 << 30) | (i2 << 21) | (i1 << 12);
                uint64_t sphys = spt[i1] & ~0xFFFULL;
                uint64_t flags = spt[i1] & 0xFFF;
                uint64_t dphys = pmm_alloc_frame();
                if (!dphys) return -1;
                uint8_t* s = (uint8_t*)(PHYSMAP_BASE + sphys);
                uint8_t* d = (uint8_t*)(PHYSMAP_BASE + dphys);
                for (int b = 0; b < 4096; b++) d[b] = s[b];
                vmm_map_page_into(dst_pml4, va, dphys, flags);
            }
        }
    }
    return 0;
}

/* Share the user half (PML4[0]) of src into dst using copy-on-write.
 *
 * For every mapped leaf page:
 *   1. Clear PAGE_RW + set PAGE_COW in the *source* PTE (makes it read-only).
 *   2. Map the same physical frame into dst with the same read-only + CoW flags.
 *   3. Increment the frame's refcount so it survives until both sides drop it.
 *
 * On the first write by either side the CPU raises a #PF (protection violation
 * on a present page), which vmm_page_fault_handler resolves by allocating a
 * private copy.
 */
int vmm_cow_user_space(uint64_t dst_pml4, uint64_t src_pml4) {
    uint64_t* spml4 = as_table(src_pml4);
    if (!(spml4[0] & PAGE_PRESENT)) return 0;
    uint64_t* spdpt = as_table(spml4[0] & ~0xFFFULL);

    for (uint64_t i3 = 0; i3 < ENTRIES; i3++) {
        if (!(spdpt[i3] & PAGE_PRESENT)) continue;
        uint64_t* spd = as_table(spdpt[i3] & ~0xFFFULL);
        for (uint64_t i2 = 0; i2 < ENTRIES; i2++) {
            if (!(spd[i2] & PAGE_PRESENT) || (spd[i2] & PAGE_PS)) continue;
            uint64_t* spt = as_table(spd[i2] & ~0xFFFULL);
            for (uint64_t i1 = 0; i1 < ENTRIES; i1++) {
                if (!(spt[i1] & PAGE_PRESENT)) continue;

                uint64_t va    = (i3 << 30) | (i2 << 21) | (i1 << 12);
                uint64_t phys  = spt[i1] & ~0xFFFULL;

                /* Shared flags: present + user + CoW, but NOT writable. */
                uint64_t flags = (spt[i1] & 0xFFF) & ~PAGE_RW;
                flags |= PAGE_COW;

                /* Demote the source page to read-only + CoW. */
                spt[i1] = phys | flags;
                __asm__ __volatile__("invlpg (%0)" :: "r"(va) : "memory");

                /* Map the same frame into the child (vmm_map_page_into adds PRESENT). */
                if (vmm_map_page_into(dst_pml4, va, phys,
                                      flags & ~PAGE_PRESENT) != 0) return -1;

                /* Both parent and child now hold a reference. */
                pmm_addref(phys);
            }
        }
    }
    return 0;
}

/* Release every user frame and every intermediate page-table frame of pml4.
 * Leaf frames are released through pmm_free_frame (refcount-aware); table
 * frames have refcount 1 (unique per AS) and are freed immediately.
 * Never call this on the kernel PML4 or on 0.
 */
void vmm_free_user_space(uint64_t pml4_phys) {
    if (!pml4_phys || pml4_phys == g_kernel_pml4) return;

    uint64_t* pml4 = as_table(pml4_phys);
    if (!(pml4[0] & PAGE_PRESENT)) goto free_pml4;

    {
        uint64_t pdpt_phys = pml4[0] & ~0xFFFULL;
        uint64_t* pdpt = as_table(pdpt_phys);
        for (uint64_t i3 = 0; i3 < ENTRIES; i3++) {
            if (!(pdpt[i3] & PAGE_PRESENT)) continue;
            uint64_t pd_phys = pdpt[i3] & ~0xFFFULL;
            uint64_t* pd = as_table(pd_phys);
            for (uint64_t i2 = 0; i2 < ENTRIES; i2++) {
                if (!(pd[i2] & PAGE_PRESENT) || (pd[i2] & PAGE_PS)) continue;
                uint64_t pt_phys = pd[i2] & ~0xFFFULL;
                uint64_t* pt = as_table(pt_phys);
                for (uint64_t i1 = 0; i1 < ENTRIES; i1++) {
                    if (pt[i1] & PAGE_PRESENT)
                        pmm_free_frame(pt[i1] & ~0xFFFULL);
                }
                pmm_free_frame(pt_phys);   /* PT frame itself (refcount 1) */
            }
            pmm_free_frame(pd_phys);       /* PD frame */
        }
        pmm_free_frame(pdpt_phys);         /* PDPT frame */
    }
free_pml4:
    pmm_free_frame(pml4_phys);             /* PML4 frame */
}

/* ── #PF handler ────────────────────────────────────────────────────────────
 * Error-code bits (Intel SDM vol.3 §6.15):
 *   bit 0 (P)  – 0 = not-present,  1 = protection violation
 *   bit 1 (W)  – 0 = read,         1 = write
 *   bit 2 (U)  – 0 = supervisor,   1 = user-mode fault
 *
 * A CoW fault is: P=1 (page present), W=1 (write), PAGE_COW set in PTE.
 * Anything else is a genuine fault → panic.
 */
void vmm_page_fault_handler(isr_frame_t* frame) {
    uint64_t va, cr3;
    __asm__ __volatile__("mov %%cr2, %0" : "=r"(va));
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
    cr3 &= ~0xFFFULL;

    uint64_t ec = frame->error_code;

    /* ── Demand-paged stack growth ──────────────────────────────────────────
     * Condition: not-present fault (P=0, bit 0 clear) in user space, VA falls
     * between the current stack low and the hard stack limit.
     * We allocate a zeroed frame, map it, and lower stack_low.
     */
    if (!(ec & 0x1) && cr3 && cr3 != g_kernel_pml4) {
        process_t* cur = scheduler_current();
        uint64_t page_va = va & ~0xFFFULL;
        if (cur && cur->stack_low > USTACK_LIMIT && page_va < cur->stack_low
                && page_va >= USTACK_LIMIT) {
            uint64_t frame = pmm_alloc_frame();
            if (!frame) {
                serial_write((const uint8_t*)"\n[noxis64] OOM on stack growth\n");
                goto panic;
            }
            /* Zero the new stack page (C assumes zero-initialised stack frames). */
            uint8_t* p = (uint8_t*)(PHYSMAP_BASE + frame);
            for (int i = 0; i < 4096; i++) p[i] = 0;
            vmm_map_page_into(cr3, page_va, frame, PAGE_RW | PAGE_USER);
            cur->stack_low = page_va;
            return;
        }
    }

    /* ── CoW fault: write to a present-but-read-only CoW page ──────────── */
    if ((ec & 0x3) == 0x3 && cr3 && cr3 != g_kernel_pml4) {
        /* Walk the page tables for this VA. */
        uint64_t i4 = (va >> 39) & 0x1FF, i3 = (va >> 30) & 0x1FF;
        uint64_t i2 = (va >> 21) & 0x1FF, i1 = (va >> 12) & 0x1FF;

        uint64_t* pml4 = as_table(cr3);
        if (!(pml4[i4] & PAGE_PRESENT)) goto panic;
        uint64_t* pdpt = as_table(pml4[i4] & ~0xFFFULL);
        if (!(pdpt[i3] & PAGE_PRESENT)) goto panic;
        uint64_t* pd   = as_table(pdpt[i3] & ~0xFFFULL);
        if (!(pd[i2] & PAGE_PRESENT) || (pd[i2] & PAGE_PS)) goto panic;
        uint64_t* pt   = as_table(pd[i2] & ~0xFFFULL);
        uint64_t  pte  = pt[i1];
        if (!(pte & PAGE_PRESENT) || !(pte & PAGE_COW)) goto panic;

        uint64_t old_phys = pte & ~0xFFFULL;
        uint64_t flags    = (pte & 0xFFF) & ~PAGE_COW;   /* strip CoW bit */

        if (pmm_refcount(old_phys) == 1) {
            /* Sole owner — just make the page writable in place. */
            pt[i1] = old_phys | flags | PAGE_RW;
        } else {
            /* Shared — allocate a private copy. */
            uint64_t new_phys = pmm_alloc_frame();
            if (!new_phys) goto panic;   /* OOM → treat as fatal */
            uint8_t* src = (uint8_t*)(PHYSMAP_BASE + old_phys);
            uint8_t* dst = (uint8_t*)(PHYSMAP_BASE + new_phys);
            for (int b = 0; b < 4096; b++) dst[b] = src[b];
            pmm_free_frame(old_phys);      /* drop our share of the old frame */
            pt[i1] = new_phys | flags | PAGE_RW;
        }
        __asm__ __volatile__("invlpg (%0)" :: "r"(va) : "memory");
        return;   /* resume the faulting instruction */
    }

panic:
    serial_write((const uint8_t*)"\n[noxis64] PAGE FAULT  err=");
    serial_write_hex64(ec);
    serial_write((const uint8_t*)"  cr2="); serial_write_hex64(va);
    serial_write((const uint8_t*)"  rip="); serial_write_hex64(frame->rip);
    serial_write((const uint8_t*)"\n[noxis64] halted.\n");
    for (;;) __asm__ __volatile__("cli; hlt");
}

/* Create a new address space: private user half, shared physmap + kernel. */
uint64_t vmm_create_address_space(void) {
    uint64_t pml4_phys = pmm_alloc_frame();
    if (!pml4_phys) return 0;
    uint64_t* pml4 = as_table(pml4_phys);
    zero_table(pml4);

    uint64_t* kpml4 = as_table(g_kernel_pml4);
    pml4[PML4_PHYSMAP] = kpml4[PML4_PHYSMAP];   /* shared physmap */
    pml4[PML4_KERNEL]  = kpml4[PML4_KERNEL];    /* shared kernel  */
    /* PML4[0] left empty — private user half. */
    return pml4_phys;
}
