# Skill: Heap Allocator

## Purpose
This skill covers implementing a kernel heap allocator (`kmalloc`/`kfree`) on top of virtual memory. The heap allocator manages variable-sized allocations, while VMM provides the underlying 4 KB pages.

## Key Concepts

### Two-Layer Architecture

```
kmalloc / kfree        ← Variable-size allocator (this skill)
    ↓
vmm_alloc_page         ← Page-granularity virtual memory
vmm_free_page
    ↓
pmm_alloc_frame        ← Physical frame allocator
```

The heap allocator never touches physical memory directly. It requests 4 KB pages from VMM and subdivides them.

### Block Metadata

Every allocated block is preceded by a header tracking its size and status:

```c
typedef struct block_header {
    uint32_t size;               /* Payload size (not including header) */
    uint32_t magic;              /* Magic number for corruption detection */
    struct block_header* prev;   /* Previous block in list */
    struct block_header* next;   /* Next block in list */
    uint8_t  is_free;            /* TRUE if block is free */
} block_header_t;

#define HEAP_MAGIC  0x1BADCAFE
#define HEADER_SIZE ALIGN_UP(sizeof(block_header_t), 16)
```

### Allocation Strategy: First-Fit

```
1. Walk the free list
2. Find the first block with size >= requested + HEADER_SIZE
3. If found, check if splitting is possible
4. If the remainder > (HEADER_SIZE + MIN_ALLOC), split into two blocks
5. Mark allocated, return pointer to payload (after header)
6. If no suitable block, request a new page from VMM, add to heap
```

### Free Strategy: Coalescing

```
1. Mark block as free
2. Check if next block is free → merge
3. Check if previous block is free → merge
4. If the merged block spans a full page, consider page release (optional)
```

### Heap Growth

```
1. Start with a few pre-allocated pages at kernel init
2. On OOM during allocation, request more pages from VMM
3. New pages are added as free blocks to the free list
4. Never shrink the heap below the initial reservation
```

## Common Pitfalls

1. **Alignment**: The returned pointer must be suitably aligned for any type (16-byte alignment is safe). The header must also be aligned, or the payload won't be. Always align after sizing.

2. **Splitting calculus wrong**: When splitting a block of size S into A (size requested) + B (remainder), B's header must fit. The minimum block size is `HEADER_SIZE + 1` (or some MIN_ALLOC). If the remainder is smaller, don't split — just give the whole block.

3. **Double free**: Freeing the same pointer twice corrupts the free list. Use magic numbers and guard pages to detect.

4. **Forgetting the header when freeing**: User passes a pointer to the payload. You must subtract HEADER_SIZE to get the header. Common mistake: `kfree` called on `&payload[0]` but treats it as the header.

5. **Wild pointers in free list**: After freeing a block, the `prev` and `next` pointers are modified. If someone holds a dangling pointer to a freed block, the linked list can be corrupted.

6. **Integer overflow in size**: `HEADER_SIZE + requested_size` can overflow. Always check before allocating.

7. **Coalescing across page boundaries**: Blocks that span multiple pages (large allocations) require careful boundary checking when coalescing. Don't coalesce past the heap's mapped virtual range.

8. **Re-entrancy**: `kmalloc` and `kfree` are called from interrupt handlers (e.g., keyboard buffer allocation). They must be interrupt-safe. At minimum, mask interrupts during free list manipulation.

## Implementation Pattern

```c
/**
 * @brief Allocates memory from the kernel heap
 * @param size  Number of bytes to allocate (will be aligned up)
 * @return Pointer to allocated memory, or NULL if OOM
 */
void* kmalloc(uint32_t size) {
    if (size == 0) return NULL;
    if (size > HEAP_MAX_ALLOC) return NULL;

    /* Align up to 16 bytes */
    size = ALIGN_UP(size, 16);

    /* Disable interrupts during allocation */
    uint32_t eflags = _eflags_get();
    _cli();

    block_header_t* block = _heap_find_first_fit(size);
    if (!block) {
        block = _heap_expand(size);
        if (!block) {
            _eflags_set(eflags);
            return NULL;
        }
    }

    _heap_split_if_possible(block, size);
    block->is_free = FALSE;
    block->magic = HEAP_MAGIC;

    _eflags_set(eflags);
    return (void*)((uint32_t)block + HEADER_SIZE);
}
```

## Debugging Tips

- After every `kmalloc`/`kfree`, walk the entire free list and verify invariants: no adjacent free blocks, all magic numbers intact, sizes consistent
- Add a sentinel value at the **end** of every block payload to detect buffer overflows on `kfree`
- Track allocation count vs free count — should be 0 when all memory is freed
- Use QEMU `info mem` to check for memory leaks (comparing before/after page counts)
- Page fault in `kmalloc`: likely the heap's virtual pages aren't mapped. Check VMM initialization.
- Corruption after `kfree`: likely a double-free or a write-after-free. The block header's `next`/`prev` were overwritten.
