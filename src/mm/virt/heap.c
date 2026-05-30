/**
 * @file    mm/heap.c
 * @brief   Kernel heap — first-fit allocator with coalescing
 * @author  Noxis Team
 * @date    2026-05-29
 */
#include <mm/virt/heap.h>
#include <mm/virt/paging.h>
#include <mm/phys/pmm.h>
#include <mm/virt/vmm.h>
#include <kernel/hal/ports.h>
#include <common/types.h>

/* ── constants ─────────────────────────────────────────────── */
#define HEAP_MAGIC      0x1BADCAFE
#define HEADER_SIZE     ((sizeof(block_header_t) + 15) & ~15)

/* ── block header ──────────────────────────────────────────── */
typedef struct block_header {
    uint32_t            magic;
    uint32_t            size;       /* payload size */
    struct block_header* prev;
    struct block_header* next;
    uint8_t             is_free;
} block_header_t;

/* ── file-scope state ──────────────────────────────────────── */
static block_header_t* g_heap_start;
static uint32_t        g_heap_size;
static uint32_t        g_free_total;

/* ── private functions ─────────────────────────────────────── */

static uint32_t _align(uint32_t v) { return (v + 15) & ~15; }

/**
 * @brief Maps new pages for the heap and appends as a free block
 */
static block_header_t* _heap_expand(uint32_t min_size) {
    uint32_t needed = _align(min_size + HEADER_SIZE);
    uint32_t pages  = (needed + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages == 0) pages = 1;

    uint32_t virt = (uint32_t)g_heap_start + g_heap_size;
    for (uint32_t i = 0; i < pages; i++) {
        uint32_t phys;
        if (pmm_alloc_frame(&phys) != OS_OK) return (block_header_t*)0;
        if (vmm_map_page(virt + i * PAGE_SIZE, phys,
                         PAGE_PRESENT | PAGE_RW) != OS_OK) {
            return (block_header_t*)0;
        }
    }

    block_header_t* block = (block_header_t*)virt;
    block->magic   = HEAP_MAGIC;
    block->size    = pages * PAGE_SIZE - HEADER_SIZE;
    block->prev    = (block_header_t*)0;
    block->next    = (block_header_t*)0;
    block->is_free = TRUE;

    g_heap_size  += pages * PAGE_SIZE;
    g_free_total += block->size;
    return block;
}

/**
 * @brief Splits a free block if the remainder is large enough
 */
static void _heap_split(block_header_t* block, uint32_t needed) {
    uint32_t remainder = block->size - needed;
    if (remainder < HEADER_SIZE + 16) return;

    block_header_t* new_block = (block_header_t*)((uint32_t)block + HEADER_SIZE + needed);
    new_block->magic   = HEAP_MAGIC;
    new_block->size    = remainder - HEADER_SIZE;
    new_block->prev    = block;
    new_block->next    = block->next;
    new_block->is_free = TRUE;

    block->size = needed;
    block->next = new_block;
    if (new_block->next) new_block->next->prev = new_block;

    g_free_total += new_block->size;
}

static void _heap_coalesce(block_header_t* block) {
    /* Merge with next */
    if (block->next && block->next->is_free) {
        block->size += HEADER_SIZE + block->next->size;
        block->next  = block->next->next;
        if (block->next) block->next->prev = block;
    }
    /* Merge with previous */
    if (block->prev && block->prev->is_free) {
        block->prev->size += HEADER_SIZE + block->size;
        block->prev->next  = block->next;
        if (block->next) block->next->prev = block->prev;
    }
}

/* ── public functions ──────────────────────────────────────── */

os_status_t heap_init(void) {
    g_heap_size  = 0;
    g_free_total = 0;
    g_heap_start = (block_header_t*)HEAP_VIRT_START;

    /* Allocate initial heap pages */
    block_header_t* first = _heap_expand(HEAP_INITIAL_PAGES * PAGE_SIZE);
    if (!first) return OS_ERR_OOM;

    return OS_OK;
}

void* kmalloc(uint32_t size) {
    if (size == 0) return (void*)0;
    uint32_t needed = _align(size);

    /* Walk the heap for a free block */
    block_header_t* block = g_heap_start;
    while (block) {
        if (block->is_free && block->size >= needed) {
            _heap_split(block, needed);
            block->is_free = FALSE;
            g_free_total -= block->size;
            return (void*)((uint32_t)block + HEADER_SIZE);
        }
        block = block->next;
    }

    /* No block found — expand heap */
    block = _heap_expand(needed);
    if (!block) return (void*)0;

    _heap_split(block, needed);
    block->is_free = FALSE;
    g_free_total -= block->size;
    return (void*)((uint32_t)block + HEADER_SIZE);
}

void kfree(void* ptr) {
    if (!ptr) return;

    block_header_t* block = (block_header_t*)((uint32_t)ptr - HEADER_SIZE);
    if (block->magic != HEAP_MAGIC) return;

    block->is_free = TRUE;
    g_free_total += block->size;
    _heap_coalesce(block);
}

uint32_t heap_get_free(void) {
    return g_free_total;
}
