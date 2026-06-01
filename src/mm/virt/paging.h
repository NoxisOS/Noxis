/**
 * @file    mm/paging.h
 * @brief   Page table constants, flags, and recursive mapping macros
 * @author  Noxis Team
 * @date    2026-05-29
 */
#ifndef MM_PAGING_H
#define MM_PAGING_H

/* ── page size constants ───────────────────────────────────── */
#define PAGE_SIZE            0x1000      /* 4 KB */
#define PAGE_TABLE_ENTRIES   1024
#define PAGE_DIR_ENTRIES     1024

/* ── page flags ────────────────────────────────────────────── */
#define PAGE_PRESENT         0x001
#define PAGE_RW              0x002
#define PAGE_USER            0x004
#define PAGE_WRITETHROUGH    0x008
#define PAGE_CACHE_DISABLE   0x010
#define PAGE_ACCESSED        0x020
#define PAGE_DIRTY           0x040
#define PAGE_SIZE_4MB        0x080
#define PAGE_GLOBAL          0x100

/* ── OS-defined bits (9–11, ignored by the CPU) ─────────────── */
#define PAGE_COW             0x200      /* copy-on-write: shared read-only,
                                           duplicated on write fault       */

/* ── recursive paging (last PDE → PD itself) ──────────────── */
#define RECURSIVE_INDEX      1023
#define RECURSIVE_VADDR      0xFFFFF000

/* ── kernel virtual address space ──────────────────────────── */
#define KERNEL_VIRT_BASE     0xC0000000
#define KERNEL_VIRT_CODE     0xC0100000

/* ── physical address extraction ──────────────────────────── */
#define PAGE_ALIGN_DOWN(a)   ((a) & ~0xFFF)
#define PAGE_ALIGN_UP(a)     (((a) + 0xFFF) & ~0xFFF)
#define PDE_INDEX(v)         ((v) >> 22)
#define PTE_INDEX(v)         (((v) >> 12) & 0x3FF)
#define PAGE_OFFSET(v)       ((v) & 0xFFF)

#endif /* MM_PAGING_H */
