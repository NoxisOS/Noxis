/**
 * @file    mm/slab.h
 * @brief   Typed slab allocator for fixed-size kernel objects.
 *
 * Each slab_cache_t manages a pool of identically-sized objects.
 * Free objects are linked through themselves (intrusive free-list),
 * so allocation and deallocation are O(1).
 *
 * Adaptive reclaim
 * ────────────────
 * Slabs grow by allocating a contiguous *block* of objects from the
 * kernel heap.  Blocks are tracked so that slab_reap() can return
 * fully-free blocks to the heap when memory pressure rises — adaptive
 * shrink that hobby kernels almost never implement.
 *
 * Usage:
 *   g_process_slab = slab_create("process_t", sizeof(process_t), 16);
 *   process_t *p   = slab_alloc(g_process_slab);  // zeroed, O(1)
 *   slab_free(g_process_slab, p);                  // O(1)
 *   uint32_t reclaimed = slab_reap(g_process_slab);// free idle blocks
 */
#ifndef MM_SLAB_H
#define MM_SLAB_H

#include <common/types.h>

/* One contiguous allocation backing several objects. */
typedef struct slab_block {
    struct slab_block *next;
    uint8_t           *base;     /* first object in this block        */
    uint32_t           n_objs;   /* objects carved from this block    */
    uint32_t           n_free;   /* how many are currently on free list*/
} slab_block_t;

typedef struct slab_cache {
    const char   *name;
    uint32_t      obj_size;     /* bytes per object (>= sizeof(void*)) */
    void         *free_head;    /* first free object (intrusive list)  */
    slab_block_t *blocks;       /* list of backing blocks              */
    uint32_t      n_alloc;      /* live allocations                    */
    uint32_t      n_free;       /* objects on the free list            */
    uint32_t      n_alloc_peak; /* high-water mark of n_alloc          */
    uint32_t      grow_by;      /* objects added per refill            */
} slab_cache_t;

/* Initialise the slab subsystem (call once, after heap_init()). */
void          slab_init(void);

/* Create a new slab cache. grow_by = objects added per refill. */
slab_cache_t *slab_create(const char *name, uint32_t obj_size,
                           uint32_t grow_by);

/* Allocate one zeroed object. Returns NULL on OOM. O(1). */
void         *slab_alloc(slab_cache_t *cache);

/* Return one object to its cache. obj must not be NULL. O(1). */
void          slab_free(slab_cache_t *cache, void *obj);

/* Return every fully-free backing block to the kernel heap.
 * Returns the number of bytes reclaimed. */
uint32_t      slab_reap(slab_cache_t *cache);

/* Reap every global cache (called under memory pressure). */
uint32_t      slab_reap_all(void);

#endif /* MM_SLAB_H */
