/**
 * @file    mm/heap.h
 * @brief   Kernel heap allocator — kmalloc / kfree
 * @author  Noxis Team
 * @date    2026-05-29
 */
#ifndef MM_HEAP_H
#define MM_HEAP_H

#include <common/types.h>
#include <common/status.h>

/* ── constants ─────────────────────────────────────────────── */
#define HEAP_INITIAL_PAGES  64      /* 256 KB initial heap */
#define HEAP_VIRT_START     0xC0400000

/* ── public functions ──────────────────────────────────────── */

/**
 * @brief Initializes the kernel heap
 * @return OS_OK on success
 */
os_status_t heap_init(void);

/**
 * @brief Allocates memory from the kernel heap
 * @param size  Number of bytes (will be aligned to 16)
 * @return Pointer to allocated memory, or NULL if OOM
 */
void* kmalloc(uint32_t size);

/**
 * @brief Frees previously allocated memory
 * @param ptr  Pointer from kmalloc (must not be NULL)
 */
void kfree(void* ptr);

/**
 * @brief Returns the total free space in the heap
 */
uint32_t heap_get_free(void);

#endif /* MM_HEAP_H */
