/**
 * @file    mm/heap.h
 * @brief   Kernel heap allocator — kmalloc / kfree
 * @author  Noxis Team
 * @date    2026-05-29
 */
#ifndef MM_HEAP_H
#define MM_HEAP_H

#include <common/types.h>
#include <common/status.h>

/* ── constants ─────────────────────────────────────────────── */
#define HEAP_INITIAL_PAGES  64      /* 256 KB initial heap */
#define HEAP_VIRT_START     0xC0400000

/* ── Allocation tags ────────────────────────────────────────
   Every kernel allocation is attributed to a subsystem so memstat
   can show a per-module breakdown — instant leak triage.        */
typedef enum {
    MEM_TAG_UNTAGGED = 0,
    MEM_TAG_SLAB,        /* slab cache blocks + bookkeeping */
    MEM_TAG_ARENA,       /* per-process arena blocks        */
    MEM_TAG_VFS,         /* file objects / dir entries      */
    MEM_TAG_FS,          /* on-disk FS buffers              */
    MEM_TAG_PIPE,        /* pipe ring buffers               */
    MEM_TAG_PROC,        /* misc process bookkeeping        */
    MEM_TAG_DRIVER,      /* driver-owned buffers            */
    MEM_TAG__COUNT
} mem_tag_t;

/* ── public functions ──────────────────────────────────────── */

/**
 * @brief Initializes the kernel heap
 * @return OS_OK on success
 */
os_status_t heap_init(void);

/**
 * @brief Allocates memory from the kernel heap, attributed to a tag.
 * @param size  Number of bytes (will be aligned to 16)
 * @param tag   Owning subsystem (see mem_tag_t)
 * @return Pointer to allocated memory, or NULL if OOM
 */
void* kmalloc_tagged(uint32_t size, mem_tag_t tag);

/**
 * @brief Allocates memory from the kernel heap (untagged convenience).
 */
void* kmalloc(uint32_t size);

/**
 * @brief Frees previously allocated memory
 * @param ptr  Pointer from kmalloc (must not be NULL)
 */
void kfree(void* ptr);

/**
 * @brief Returns the total free space in the heap
 */
uint32_t heap_get_free(void);

/* ── Tag introspection (for memstat) ────────────────────────── */
uint32_t    heap_tag_bytes(mem_tag_t tag);   /* live bytes for a tag */
uint32_t    heap_tag_allocs(mem_tag_t tag);  /* live alloc count     */
const char* heap_tag_name(mem_tag_t tag);    /* human-readable name  */

#endif /* MM_HEAP_H */
