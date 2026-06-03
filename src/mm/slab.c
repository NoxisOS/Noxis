/**
 * @file    mm/slab.c
 * @brief   Typed slab allocator — O(1) alloc/free + adaptive reclaim.
 *
 * Free-list: when an object is free, its first 4 bytes hold a pointer
 * to the next free object.  When live, those bytes belong to the caller.
 *
 * Blocks: each refill kmalloc()s one block of `grow_by` objects and
 * records it in cache->blocks.  Each block tracks how many of its own
 * objects are currently free, so slab_reap() can identify and release
 * blocks that are entirely idle.
 */
#include <mm/slab.h>
#include <mm/virt/heap.h>
#include <common/types.h>

static slab_cache_t *g_all_caches[8];
static uint32_t      g_n_caches = 0;

/* Caches are created on demand via slab_create(); nothing preallocated. */
void slab_init(void) { }

/* ── slab_create ──────────────────────────────────────────── */

slab_cache_t *slab_create(const char *name, uint32_t obj_size,
                           uint32_t grow_by) {
    if (obj_size < sizeof(void *)) obj_size = sizeof(void *);

    slab_cache_t *c = (slab_cache_t*)kmalloc_tagged(sizeof(slab_cache_t),
                                                    MEM_TAG_SLAB);
    if (!c) return (slab_cache_t*)0;

    c->name         = name;
    c->obj_size     = obj_size;
    c->free_head    = (void*)0;
    c->blocks       = (slab_block_t*)0;
    c->n_alloc      = 0;
    c->n_free       = 0;
    c->n_alloc_peak = 0;
    c->grow_by      = grow_by ? grow_by : 8;

    if (g_n_caches < 8) g_all_caches[g_n_caches++] = c;
    return c;
}

/* ── Internal: which block owns this object? ─────────────── */

static slab_block_t *_block_of(slab_cache_t *c, void *obj) {
    uint8_t *p = (uint8_t*)obj;
    for (slab_block_t *b = c->blocks; b; b = b->next) {
        uint8_t *lo = b->base;
        uint8_t *hi = b->base + b->n_objs * c->obj_size;
        if (p >= lo && p < hi) return b;
    }
    return (slab_block_t*)0;
}

/* ── Internal: grow the free list by one block ───────────── */

static int _slab_grow(slab_cache_t *c) {
    slab_block_t *b = (slab_block_t*)kmalloc_tagged(sizeof(slab_block_t),
                                                    MEM_TAG_SLAB);
    if (!b) return 0;

    uint8_t *mem = (uint8_t*)kmalloc_tagged(c->obj_size * c->grow_by,
                                            MEM_TAG_SLAB);
    if (!mem) { kfree(b); return 0; }

    b->base   = mem;
    b->n_objs = c->grow_by;
    b->n_free = c->grow_by;
    b->next   = c->blocks;
    c->blocks = b;

    for (uint32_t i = 0; i < c->grow_by; i++) {
        void *obj     = mem + i * c->obj_size;
        *(void **)obj = c->free_head;
        c->free_head  = obj;
        c->n_free++;
    }
    return 1;
}

/* ── slab_alloc ───────────────────────────────────────────── */

void *slab_alloc(slab_cache_t *c) {
    if (!c) return (void*)0;

    if (!c->free_head && !_slab_grow(c))
        return (void*)0;

    void *obj    = c->free_head;
    c->free_head = *(void **)obj;
    c->n_free--;
    c->n_alloc++;
    if (c->n_alloc > c->n_alloc_peak) c->n_alloc_peak = c->n_alloc;

    /* Account the object against its block. */
    slab_block_t *b = _block_of(c, obj);
    if (b && b->n_free) b->n_free--;

    /* Zero the object. */
    { uint8_t *p=(uint8_t*)obj; uint32_t n=c->obj_size; while(n--) *p++=0; }
    return obj;
}

/* ── slab_free ────────────────────────────────────────────── */

void slab_free(slab_cache_t *c, void *obj) {
    if (!c || !obj) return;

    *(uint32_t *)obj = 0xDEADC0DE;   /* poison */

    *(void **)obj = c->free_head;
    c->free_head  = obj;
    c->n_free++;
    c->n_alloc--;

    slab_block_t *b = _block_of(c, obj);
    if (b) b->n_free++;
}

/* ── slab_reap ────────────────────────────────────────────── */
/* Release every block whose objects are all on the free list.
   Returns bytes reclaimed (block payload + bookkeeping).         */

uint32_t slab_reap(slab_cache_t *c) {
    if (!c) return 0;
    uint32_t reclaimed = 0;

    slab_block_t **pp = &c->blocks;
    while (*pp) {
        slab_block_t *b = *pp;

        if (b->n_free != b->n_objs) { pp = &b->next; continue; }

        /* This block is entirely free — unlink each of its objects
           from the intrusive free list before releasing the block. */
        uint8_t *lo = b->base;
        uint8_t *hi = b->base + b->n_objs * c->obj_size;

        void **slot = &c->free_head;
        while (*slot) {
            uint8_t *o = (uint8_t*)(*slot);
            if (o >= lo && o < hi) {
                *slot = *(void **)o;   /* unlink */
                c->n_free--;
            } else {
                slot = (void **)(*slot);
            }
        }

        *pp = b->next;                 /* unlink block from cache */
        reclaimed += c->obj_size * b->n_objs + (uint32_t)sizeof(slab_block_t);
        kfree(b->base);
        kfree(b);
    }
    return reclaimed;
}

uint32_t slab_reap_all(void) {
    uint32_t total = 0;
    for (uint32_t i = 0; i < g_n_caches; i++)
        total += slab_reap(g_all_caches[i]);
    return total;
}
