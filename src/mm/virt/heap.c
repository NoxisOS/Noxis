/**
 * @file    mm/virt/heap.c
 * @brief   Kernel heap — first-fit kmalloc/kfree with coalescing (x86-64).
 *
 * Carves a region out of the identity-mapped low memory (16 MB..48 MB).
 */
#include <mm/virt/heap.h>
#include <drivers/serial.h>

void serial_write_hex64(uint64_t v);

/* Heap lives at physical 16 MB..48 MB, accessed through the physmap so it
   is reachable from every address space (kernel data must not sit in the
   per-process low half). */
#define PHYSMAP_BASE  0xFFFF800000000000ULL
#define HEAP_BASE   (PHYSMAP_BASE + 16ULL * 1024 * 1024)
#define HEAP_SIZE   (32ULL * 1024 * 1024)
#define ALIGN16(n)  (((n) + 15) & ~15ULL)
#define MAGIC       0xB10C600DB10C600DULL
#define POISON      0xDEADC0DEDEADC0DEULL

typedef struct block {
    uint64_t       magic;
    uint64_t       size;
    struct block*  next;   /* bit 0 = free flag; mask before deref */
    struct block*  prev;
} block_t;

#define BLK_FREE_BIT    ((uintptr_t)1)
#define blk_next(b)     ((block_t*)((uintptr_t)(b)->next & ~BLK_FREE_BIT))
#define blk_is_free(b)  ((uintptr_t)(b)->next & BLK_FREE_BIT)
#define blk_set_free(b) ((b)->next = (block_t*)((uintptr_t)(b)->next |  BLK_FREE_BIT))
#define blk_set_used(b) ((b)->next = (block_t*)((uintptr_t)(b)->next & ~BLK_FREE_BIT))

static block_t* g_head;

os_status_t heap_init(void) {
    g_head = (block_t*)HEAP_BASE;
    g_head->magic = MAGIC;
    g_head->size  = HEAP_SIZE - sizeof(block_t);
    g_head->next  = NULL;
    g_head->prev  = NULL;
    blk_set_free(g_head);
    serial_write((const uint8_t*)"[noxis64] heap base="); serial_write_hex64(HEAP_BASE);
    serial_write((const uint8_t*)" size=");                serial_write_hex64(HEAP_SIZE);
    serial_write((const uint8_t*)"\n");
    return OS_OK;
}

void* kmalloc(uint64_t want) {
    if (want == 0) return NULL;
    want = ALIGN16(want);
    for (block_t* b = g_head; b; b = blk_next(b)) {
        if (!blk_is_free(b) || b->size < want) continue;
        if (b->size >= want + sizeof(block_t) + 16) {
            block_t* nb = (block_t*)((uint8_t*)b + sizeof(block_t) + want);
            nb->magic = MAGIC; nb->size = b->size - want - sizeof(block_t);
            block_t* b_old_next = blk_next(b);
            nb->next = b_old_next; nb->prev = b;
            blk_set_free(nb);
            if (b_old_next) b_old_next->prev = nb;
            b->next = nb; b->size = want; /* bit 0 = 0: used */
        }
        blk_set_used(b); /* clears bit 0, next ptr already correct */
        return (void*)((uint8_t*)b + sizeof(block_t));
    }
    return NULL;
}

void* kmalloc_tagged(uint64_t size, mem_tag_t tag) { (void)tag; return kmalloc(size); }

void kfree(void* p) {
    if (!p) return;
    block_t* b = (block_t*)((uint8_t*)p - sizeof(block_t));
    if (b->magic != MAGIC) return;
    blk_set_free(b);
    uint64_t* q = (uint64_t*)p;
    for (uint64_t i = 0; i < b->size / 8; i++) q[i] = POISON;
    block_t* bn = blk_next(b);
    if (bn && blk_is_free(bn)) {
        b->size += sizeof(block_t) + bn->size;
        b->next = (block_t*)((uintptr_t)blk_next(bn) | BLK_FREE_BIT);
        if (blk_next(bn)) blk_next(bn)->prev = b;
    }
    if (b->prev && blk_is_free(b->prev)) {
        block_t* bp = b->prev;
        bp->size += sizeof(block_t) + b->size;
        bp->next = (block_t*)((uintptr_t)blk_next(b) | BLK_FREE_BIT);
        if (blk_next(b)) blk_next(b)->prev = bp;
    }
}

uint64_t heap_get_free(void) {
    uint64_t f = 0;
    for (block_t* b = g_head; b; b = blk_next(b)) if (blk_is_free(b)) f += b->size;
    return f;
}
