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

/* Build a fresh PML4 (identity-maps low RAM with 2 MB pages) and load it. */
os_status_t vmm_init(void);

/* Map one 4 KB page va → pa, allocating page-table levels as needed. */
int vmm_map_page(uint64_t va, uint64_t pa, uint64_t flags);

#endif /* MM_VMM_H */
