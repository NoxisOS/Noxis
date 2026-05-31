/**
 * @file    noxlib/stdlib/malloc.c
 * @brief   Kernel heap allocator — first-fit free list over brk/sbrk
 *
 * Layout in the heap:
 *   [ block_hdr_t | <payload> ] [ block_hdr_t | <payload> ] …
 *
 * The header sits immediately before the user data pointer returned
 * by malloc().  free() recovers the header via (ptr - 1).
 *
 * Coalescing: on free(), adjacent right-hand free blocks are merged.
 * Simple and sufficient for a single-process userland workload.
 */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Raw brk syscall: 0 → query, N → set break to N, returns new break. */
extern int _sys_brk(unsigned int addr);

/* ── sbrk ──────────────────────────────────────────────────────────── */

static uintptr_t _heap_end = 0;

static void *_sbrk(int delta)
{
    if (_heap_end == 0)
        _heap_end = (uintptr_t)_sys_brk(0);

    uintptr_t old     = _heap_end;
    uintptr_t new_end = _heap_end + (intptr_t)delta;

    if ((uintptr_t)_sys_brk(new_end) != new_end)
        return (void *)-1;   /* OOM */

    _heap_end = new_end;
    return (void *)old;
}

/* ── Block header ──────────────────────────────────────────────────── */

#define ALIGN4(n)   (((n) + 3u) & ~3u)

typedef struct block_hdr {
    uint32_t          size;   /* payload bytes (not including this header) */
    uint32_t          free;   /* 1 = free, 0 = in use                      */
    struct block_hdr *next;   /* next block in list (NULL = last)           */
} block_hdr_t;

#define HDR_SZ  sizeof(block_hdr_t)

static block_hdr_t *_head = NULL;

/* Grow the heap by (HDR_SZ + size) bytes, return a new in-use block. */
static block_hdr_t *_extend(size_t size)
{
    block_hdr_t *blk = (block_hdr_t *)_sbrk((int)(HDR_SZ + size));
    if (blk == (void *)-1) return NULL;
    blk->size = size;
    blk->free = 0;
    blk->next = NULL;
    return blk;
}

/* ── Public API ────────────────────────────────────────────────────── */

void *malloc(size_t size)
{
    if (!size) return NULL;
    size = ALIGN4(size);

    /* Bootstrap: allocate very first block. */
    if (!_head) {
        _head = _extend(size);
        return _head ? (void *)(_head + 1) : NULL;
    }

    /* First-fit walk. */
    block_hdr_t *blk  = _head;
    block_hdr_t *prev = NULL;

    while (blk) {
        if (blk->free && blk->size >= size) {
            blk->free = 0;
            return (void *)(blk + 1);
        }
        prev = blk;
        blk  = blk->next;
    }

    /* No fitting block — extend heap. */
    block_hdr_t *new_blk = _extend(size);
    if (!new_blk) return NULL;
    if (prev) prev->next = new_blk;
    return (void *)(new_blk + 1);
}

void *calloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    void  *p     = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void free(void *ptr)
{
    if (!ptr) return;
    block_hdr_t *blk = (block_hdr_t *)ptr - 1;
    blk->free = 1;

    /* Coalesce with contiguous free right-hand neighbours. */
    while (blk->next && blk->next->free) {
        blk->size += HDR_SZ + blk->next->size;
        blk->next  = blk->next->next;
    }
}
