/**
 * @file    mm/phys/pmm.h
 * @brief   Physical memory manager — bitmap frame allocator (x86-64).
 * @author  Noxis Team
 */
#ifndef MM_PMM_H
#define MM_PMM_H

#include <common/types.h>
#include <common/status.h>

#define PMM_FRAME_SIZE  0x1000

/* Initialise the allocator over `ram_bytes` of RAM (reserves low memory). */
os_status_t pmm_init(uint64_t ram_bytes);

/* Allocate one 4 KB frame; returns its physical address, or 0 on OOM. */
uint64_t    pmm_alloc_frame(void);

/* Free a frame by physical address. */
void        pmm_free_frame(uint64_t phys);

/* Number of free frames. */
uint64_t    pmm_free_count(void);

/* Increment the reference count of a frame (used by CoW fork). */
void        pmm_addref(uint64_t phys);

/* Return the current reference count of a frame. */
uint8_t     pmm_refcount(uint64_t phys);

#endif /* MM_PMM_H */
