# Skill: Paging

## Purpose
This skill covers x86 32-bit paging: page directories, page tables, virtual-to-physical address translation, and the recursive page directory technique for accessing page structures.

## Key Concepts

### Two-Level Paging (x86 32-bit without PAE)

A 32-bit virtual address is split into three parts:

```
31                22 21                12 11              0
┌───────────────────┬───────────────────┬─────────────────┐
│  Page Directory   │   Page Table      │   Page Offset   │
│  Index (10 bits)  │   Index (10 bits) │   (12 bits)     │
└───────────────────┴───────────────────┴─────────────────┘
```

1. CR3 → Page Directory Physical Address (must be page-aligned, i.e., bits 11-0 = 0)
2. Page Directory Entry (PDE) → Page Table Physical Address (also page-aligned)
3. Page Table Entry (PTE) → Physical Page Frame Address
4. Combine with offset → final physical address

### Page Directory (1024 entries × 4 bytes = 4096 bytes = 1 page)

Each PDE covers 4 MB of virtual address space (1024 PTEs × 4096 bytes).

### Page Table (1024 entries × 4 bytes = 4096 bytes = 1 page)

Each PTE covers 4 KB of virtual address space (one page).

### Entry Format

```
Bit 31 .............................. Bit 0
┌────────────────────────────────────────┐
│ Physical Address [31:12] │Avl│G│S│D│A│C│W│U│R│P│
└────────────────────────────────────────┘
   ^                         ^   ^ ^ ^ ^ ^ ^ ^ ^ ^
   │                         │   │ │ │ │ │ │ │ │ └── Present
   │                         │   │ │ │ │ │ │ │ └──── Read/Write
   │                         │   │ │ │ │ │ │ └────── User/Supervisor
   │                         │   │ │ │ │ │ └──────── Write-Through
   │                         │   │ │ │ │ └────────── Cache Disable
   │                         │   │ │ │ └──────────── Accessed (CPU sets)
   │                         │   │ │ └────────────── Dirty (PTE only, CPU sets)
   │                         │   │ └──────────────── Page Size (PDE only, 0=4KB)
   │                         │ └──────────────────── Global (ignored if PGE=0)
   │                         └────────────────────── Available for OS
   └──────────────────────────────────────────────── Address bits 12-31
```

### 4 MB Pages

If a PDE has the Page Size bit (bit 7) set:
- The PDE directly maps a 4 MB page (no page table involved)
- Bits 21-0 of the virtual address are the offset within the 4 MB page
- Address bits 22-31 from PDE, bits 0-21 from virtual address

This is useful for kernel identity mapping: one PDE covers the entire kernel's initial 4 MB.

### Recursive Paging (Self-Referencing Page Directory)

**The trick:** Set the last PDE (index 1023) to point to the physical address of the page directory itself.

This creates a clever address space:
- `0xFFFFF000` → The page directory itself (PDE 1023 → PD, looking at byte 0 of PD)
- `0xFFC00000 + i*0x400000` → Access page table for PDE `i`
- `0xFFC00000 + i*0x400000 + j*0x1000` → Access PTE for PDE `i`, PTE `j`

This is essential because after paging is enabled, physical addresses are no longer directly accessible — you must map page structures somewhere in the virtual address space to modify them.

### TLB (Translation Lookaside Buffer)

The CPU caches virtual→physical translations. After modifying page tables, you must invalidate:
- `invlpg [addr]` — invalidate single page
- `mov cr3, eax` — flush entire TLB (reload CR3 with same value works)

## Common Pitfalls

1. **Physical vs virtual address confusion**: CR3 takes a **physical** address. Page table entries hold **physical** addresses. But when you access page tables through recursive mapping, you use **virtual** addresses. This is the #1 source of paging bugs.

2. **Not page-aligning**: CR3 must be page-aligned (bits 11-0 = 0). Page table addresses in PDEs must be page-aligned. Frame addresses in PTEs must be page-aligned. One misaligned address = #PF or silent corruption.

3. **Identity mapping during transition**: When you enable paging (set CR0.PG), the instruction after `mov cr0, eax` must be identity-mapped — i.e., its virtual and physical addresses must match. Otherwise the CPU fetches garbage. This is why we identity-map at least the kernel's 4 MB region.

4. **Recursive mapping eating virtual space**: PDE 1023 → PD means the last 4 MB of virtual space (0xFFC00000–0xFFFFFFFF) are consumed. Plan for this.

5. **Not clearing existing entries**: When you allocate a new page table, its entries are uninitialized (random garbage). Always zero-fill new page tables before populating them.

6. **Forgetting to flush TLB**: Changing a PTE for an already-mapped page requires TLB invalidation. The CPU will use the stale cached translation otherwise.

7. **Write-Protect bit (CR0.WP)**: Set CR0.WP=1 so that the kernel cannot write to read-only pages. Without this, kernel code can accidentally corrupt user pages that are marked read-only. This is essential for correct Copy-On-Write.

8. **Self-modifying code with W^X**: If you enforce W^X (write XOR execute), you must flush the TLB after changing a page from writable to executable (or vice versa).

## Implementation Pattern

```c
/**
 * @brief Maps a 4 KB page in the current address space
 * @param virt   Virtual address to map (must be page-aligned)
 * @param phys   Physical address to map to (must be page-aligned)
 * @param flags  Page flags (PAGE_PRESENT | PAGE_RW | PAGE_USER, etc.)
 * @return OS_OK on success
 */
os_status_t vmm_map_page(uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t pd_index = virt >> 22;
    uint32_t pt_index = (virt >> 12) & 0x3FF;

    /* Get PDE via recursive mapping */
    uint32_t* pd = (uint32_t*)0xFFFFF000;
    uint32_t* pt = (uint32_t*)(0xFFC00000 + pd_index * 0x1000);

    /* Allocate page table if not present */
    if (!(pd[pd_index] & PAGE_PRESENT)) {
        uint32_t pt_phys;
        if (pmm_alloc_frame(&pt_phys) != OS_OK) return OS_ERR_OOM;
        pd[pd_index] = pt_phys | (flags & 0xFFF) | PAGE_RW;
        _vmm_zero_page((uint32_t*)pt);
    }

    /* Map the page */
    pt[pt_index] = phys | (flags & 0xFFF);
    _vmm_flush_tlb_entry(virt);
    return OS_OK;
}
```

## Debugging Tips

- Use `info mem` in QEMU monitor to see virtual memory mappings
- Use `info tlb` in QEMU monitor to see TLB entries
- Page fault (vector 14): read CR2 (faulting address), check error code on stack
  - Bit 0: 0 = not-present, 1 = protection violation
  - Bit 1: 0 = read, 1 = write
  - Bit 2: 0 = supervisor, 1 = user
  - Bit 3: 1 = reserved bit violation
  - Bit 4: 1 = instruction fetch
- Use GDB `x/1024wx 0xFFFFF000` to dump the page directory (via recursive mapping)
- Use GDB `x/1024wx 0xFFC00000` to dump the first page table
