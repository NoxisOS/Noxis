/**
 * @file    mm/arena.c
 * @brief   Arena allocator implementation.
 */
#include <mm/arena.h>
#include <mm/virt/heap.h>
#include <common/types.h>

static void _zero(void *p, uint32_t n) {
    uint8_t *b = (uint8_t*)p;
    while (n--) *b++ = 0;
}

/* ── Internal: allocate one block from the kernel heap ─────── */

static arena_block_t *_block_new(uint32_t min_cap) {
    uint32_t total = (min_cap > ARENA_BLOCK_SIZE - sizeof(arena_block_t))
                   ? sizeof(arena_block_t) + min_cap
                   : ARENA_BLOCK_SIZE;

    arena_block_t *b = (arena_block_t*)kmalloc_tagged(total, MEM_TAG_ARENA);
    if (!b) return (arena_block_t*)0;

    b->next = (arena_block_t*)0;
    b->used = 0;
    b->cap  = total - (uint32_t)sizeof(arena_block_t);
    return b;
}

/* ── arena_create ─────────────────────────────────────────── */

arena_t *arena_create(void) {
    arena_t *a = (arena_t*)kmalloc_tagged(sizeof(arena_t), MEM_TAG_ARENA);
    if (!a) return (arena_t*)0;

    a->head = _block_new(0);
    if (!a->head) { kfree(a); return (arena_t*)0; }

    a->n_alloc  = 0;
    a->n_blocks = 1;
    return a;
}

/* ── arena_alloc ──────────────────────────────────────────── */

void *arena_alloc(arena_t *a, uint32_t size) {
    if (!a || size == 0) return (void*)0;

    /* Align up to 4 bytes. */
    size = (size + 3u) & ~3u;

    /* Large objects get their own dedicated block. */
    if (size > ARENA_LARGE_THRESH) {
        arena_block_t *b = _block_new(size);
        if (!b) return (void*)0;
        b->used    = size;
        /* Prepend AFTER the current head so normal bump-alloc
           continues from the existing block on the next call. */
        b->next    = a->head ? a->head->next : (arena_block_t*)0;
        if (a->head) a->head->next = b;
        else         a->head       = b;
        a->n_blocks++;
        a->n_alloc += size;
        _zero(b->data, size);
        return (void*)b->data;
    }

    /* Does the current block have room? */
    if (!a->head || a->head->used + size > a->head->cap) {
        arena_block_t *b = _block_new(0);
        if (!b) return (void*)0;
        b->next  = a->head;
        a->head  = b;
        a->n_blocks++;
    }

    void *ptr = (void*)(a->head->data + a->head->used);
    a->head->used += size;
    a->n_alloc    += size;
    _zero(ptr, size);
    return ptr;
}

/* ── arena_reset ──────────────────────────────────────────── */

void arena_reset(arena_t *a) {
    if (!a) return;
    for (arena_block_t *b = a->head; b; b = b->next)
        b->used = 0;
    a->n_alloc = 0;
}

/* ── arena_destroy ────────────────────────────────────────── */

void arena_destroy(arena_t *a) {
    if (!a) return;

    arena_block_t *b = a->head;
    while (b) {
        arena_block_t *next = b->next;
        kfree(b);
        b = next;
    }
    kfree(a);
    /* Caller must not use `a` after this point. */
}
