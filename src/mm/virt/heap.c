/**
 * @file    mm/virt/heap.c
 * @brief   Kernel heap — first-fit kmalloc/kfree with coalescing (x86-64).
 *
 * Carves a region out of the identity-mapped low memory (16 MB..48 MB).
 */
#include <mm/virt/heap.h>
#include <drivers/serial.h>

void serial_write_hex64(uint64_t v);

#define HEAP_BASE   (16ULL * 1024 * 1024)
#define HEAP_SIZE   (32ULL * 1024 * 1024)
#define ALIGN16(n)  (((n) + 15) & ~15ULL)
#define MAGIC       0xB10C600DB10C600DULL
#define POISON      0xDEADC0DEDEADC0DEULL

typedef struct block {
    uint64_t       magic;
    uint64_t       size;
    int            free;
    struct block*  next;
    struct block*  prev;
} block_t;

static block_t* g_head;

os_status_t heap_init(void) {
    g_head = (block_t*)HEAP_BASE;
    g_head->magic = MAGIC;
    g_head->size  = HEAP_SIZE - sizeof(block_t);
    g_head->free  = 1;
    g_head->next  = NULL;
    g_head->prev  = NULL;
    serial_write((const uint8_t*)"[noxis64] heap base="); serial_write_hex64(HEAP_BASE);
    serial_write((const uint8_t*)" size=");                serial_write_hex64(HEAP_SIZE);
    serial_write((const uint8_t*)"\n");
    return OS_OK;
}

void* kmalloc(uint64_t want) {
    if (want == 0) return NULL;
    want = ALIGN16(want);
    for (block_t* b = g_head; b; b = b->next) {
        if (!b->free || b->size < want) continue;
        if (b->size >= want + sizeof(block_t) + 16) {
            block_t* nb = (block_t*)((uint8_t*)b + sizeof(block_t) + want);
            nb->magic = MAGIC; nb->size = b->size - want - sizeof(block_t);
            nb->free = 1; nb->next = b->next; nb->prev = b;
            if (b->next) b->next->prev = nb;
            b->next = nb; b->size = want;
        }
        b->free = 0;
        return (void*)((uint8_t*)b + sizeof(block_t));
    }
    return NULL;
}

void* kmalloc_tagged(uint64_t size, mem_tag_t tag) { (void)tag; return kmalloc(size); }

void kfree(void* p) {
    if (!p) return;
    block_t* b = (block_t*)((uint8_t*)p - sizeof(block_t));
    if (b->magic != MAGIC) return;
    b->free = 1;
    uint64_t* q = (uint64_t*)p;
    for (uint64_t i = 0; i < b->size / 8; i++) q[i] = POISON;
    if (b->next && b->next->free) {
        b->size += sizeof(block_t) + b->next->size;
        b->next = b->next->next;
        if (b->next) b->next->prev = b;
    }
    if (b->prev && b->prev->free) {
        b->prev->size += sizeof(block_t) + b->size;
        b->prev->next = b->next;
        if (b->next) b->next->prev = b->prev;
    }
}

uint64_t heap_get_free(void) {
    uint64_t f = 0;
    for (block_t* b = g_head; b; b = b->next) if (b->free) f += b->size;
    return f;
}
