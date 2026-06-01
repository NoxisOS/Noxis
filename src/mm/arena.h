/**
 * @file    mm/arena.h
 * @brief   Arena (region) allocator — O(1) alloc, O(blocks) destroy.
 *
 * An arena is a linked list of fixed-size blocks.  Allocation is a
 * simple pointer bump — no headers, no free lists, no fragmentation.
 * Freeing individual objects is intentionally unsupported: the entire
 * arena is destroyed at once when its owner (a process) exits.
 *
 * Why this matters
 * ────────────────
 * Traditional allocators track each object so they can free it later.
 * That tracking has cost: headers, free lists, coalescing, lock
 * contention.  If all allocations share a lifetime (e.g. "live as
 * long as this process"), the tracking is wasted work.
 *
 * With arenas:
 *   alloc  → bump pointer, O(1), no lock needed per-alloc
 *   destroy→ walk block list, free each block, O(blocks)
 *            For a typical process, blocks ≈ 1-4. Effectively O(1).
 *
 * Usage
 * ─────
 *   // Per-process kernel arena, created in proc_spawn():
 *   proc->arena = arena_create();
 *
 *   // Kernel-side argv copy in execve():
 *   char *buf = arena_alloc(proc->arena, 512);
 *
 *   // Process death (proc_terminate()):
 *   arena_destroy(proc->arena);   // frees buf and everything else
 */
#ifndef MM_ARENA_H
#define MM_ARENA_H

#include <common/types.h>

/* Block size: 4 KB (one page).  Most process arenas need only one. */
#define ARENA_BLOCK_SIZE   4096u
/* Objects larger than this go in their own dedicated block.         */
#define ARENA_LARGE_THRESH (ARENA_BLOCK_SIZE / 2u)

/* ── Types ──────────────────────────────────────────────────────── */

typedef struct arena_block {
    struct arena_block *next;
    uint32_t            used;   /* bytes consumed in data[] */
    uint32_t            cap;    /* usable bytes  in data[] */
    uint8_t             data[]; /* flexible array member   */
} arena_block_t;

typedef struct arena {
    arena_block_t *head;      /* most recently added block  */
    uint32_t       n_alloc;   /* total bytes allocated      */
    uint32_t       n_blocks;  /* total blocks allocated     */
} arena_t;

/* ── API ─────────────────────────────────────────────────────────  */

/**
 * @brief  Create a new empty arena (allocates the first block now).
 * @return Pointer to arena, or NULL on OOM.
 */
arena_t *arena_create(void);

/**
 * @brief  Allocate `size` bytes from the arena.
 *         The returned pointer is always 4-byte aligned.
 *         Do NOT call kfree() on it — it belongs to the arena.
 * @return Zeroed memory, or NULL on OOM.
 */
void    *arena_alloc(arena_t *a, uint32_t size);

/**
 * @brief  Reset all blocks to empty without freeing them.
 *         Useful for reusing an arena across multiple short-lived
 *         operations (e.g. repeated exec calls on the same thread).
 */
void     arena_reset(arena_t *a);

/**
 * @brief  Destroy the arena and free all its memory.
 *         The arena_t itself is also freed.
 *         After this call, `a` is invalid.
 */
void     arena_destroy(arena_t *a);

#endif /* MM_ARENA_H */
