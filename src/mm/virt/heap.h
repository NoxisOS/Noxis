/**
 * @file    mm/virt/heap.h
 * @brief   Kernel heap — kmalloc / kfree (x86-64).
 * @author  Noxis Team
 */
#ifndef MM_HEAP_H
#define MM_HEAP_H

#include <common/types.h>
#include <common/status.h>

/* Allocation tags (kept for API compatibility; ignored for now). */
typedef enum {
    MEM_TAG_UNTAGGED = 0,
    MEM_TAG_SLAB, MEM_TAG_ARENA, MEM_TAG_VFS, MEM_TAG_FS,
    MEM_TAG_PIPE, MEM_TAG_PROC, MEM_TAG_DRIVER,
    MEM_TAG__COUNT
} mem_tag_t;

os_status_t heap_init(void);
void*       kmalloc(uint64_t size);
void*       kmalloc_tagged(uint64_t size, mem_tag_t tag);
void        kfree(void* ptr);
uint64_t    heap_get_free(void);

#endif /* MM_HEAP_H */
