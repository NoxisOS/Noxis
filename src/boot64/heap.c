/**
 * @file    src/boot64/heap.c
 * @brief   Kernel heap — first-fit kmalloc/kfree with block coalescing.
 *
 * Carves a region out of the identity-mapped low memory.  Each block has a
 * header; freed neighbours are merged.  Use-after-free is poisoned so a
 * stale read is obvious in a debugger.
 */
#include "types.h"

void serial_write(const char* s);
void serial_hex(uint64_t v);

/* Heap lives at 16 MB..48 MB — inside the boot's 1 GB identity map. */
#define HEAP_BASE   (16ULL * 1024 * 1024)
#define HEAP_SIZE   (32ULL * 1024 * 1024)
#define ALIGN16(n)  (((n) + 15) & ~15ULL)
#define MAGIC       0xB10C600DB10C600DULL
#define POISON      0xDEADC0DEDEADC0DEULL

typedef struct block {
    uint64_t       magic;
    uint64_t       size;     /* usable bytes (excl. header) */
    int            free;
    struct block*  next;
    struct block*  prev;
} block_t;

static block_t* g_head;

void heap_init(void) {
    g_head = (block_t*)HEAP_BASE;
    g_head->magic = MAGIC;
    g_head->size  = HEAP_SIZE - sizeof(block_t);
    g_head->free  = 1;
    g_head->next  = NULL;
    g_head->prev  = NULL;
    serial_write("[noxis64] heap: base="); serial_hex(HEAP_BASE);
    serial_write(" size="); serial_hex(HEAP_SIZE); serial_write("\n");
}

void* kmalloc(uint64_t want) {
    if (want == 0) return NULL;
    want = ALIGN16(want);

    for (block_t* b = g_head; b; b = b->next) {
        if (!b->free || b->size < want) continue;

        /* Split if there's room for another header + a little payload. */
        if (b->size >= want + sizeof(block_t) + 16) {
            block_t* nb = (block_t*)((uint8_t*)b + sizeof(block_t) + want);
            nb->magic = MAGIC;
            nb->size  = b->size - want - sizeof(block_t);
            nb->free  = 1;
            nb->next  = b->next;
            nb->prev  = b;
            if (b->next) b->next->prev = nb;
            b->next = nb;
            b->size = want;
        }
        b->free = 0;
        return (void*)((uint8_t*)b + sizeof(block_t));
    }
    return NULL;  /* out of heap */
}

void kfree(void* p) {
    if (!p) return;
    block_t* b = (block_t*)((uint8_t*)p - sizeof(block_t));
    if (b->magic != MAGIC) {
        serial_write("[noxis64] kfree: bad magic at "); serial_hex((uint64_t)p);
        serial_write("\n");
        return;
    }
    b->free = 1;

    /* Poison the payload so use-after-free shows up. */
    uint64_t* q = (uint64_t*)p;
    for (uint64_t i = 0; i < b->size / 8; i++) q[i] = POISON;

    /* Coalesce with the next block if free. */
    if (b->next && b->next->free) {
        b->size += sizeof(block_t) + b->next->size;
        b->next = b->next->next;
        if (b->next) b->next->prev = b;
    }
    /* Coalesce with the previous block if free. */
    if (b->prev && b->prev->free) {
        b->prev->size += sizeof(block_t) + b->size;
        b->prev->next = b->next;
        if (b->next) b->next->prev = b->prev;
    }
}
