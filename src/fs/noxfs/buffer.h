/**
 * @file    fs/buffer.h
 * @brief   Buffer cache — caches disk blocks in RAM, provides bread/bwrite.
 *          Simple LRU eviction, hash-table lookup.
 * @author  Noxis Team
 * @date    2026-05-30
 */
#ifndef FS_BUFFER_H
#define FS_BUFFER_H

#include <common/types.h>

#define BUF_DEV_ATA  0
#define BUF_SIZE     512

typedef struct buf {
    uint32_t  dev;          /* device number */
    uint32_t  blockno;      /* LBA sector */
    uint32_t  flags;
    uint8_t   data[BUF_SIZE];
    struct buf* hash_next;  /* hash chain */
    struct buf* lru_prev;   /* doubly-linked LRU */
    struct buf* lru_next;
} buf_t;

#define B_VALID  0x01       /* data has been read from disk */
#define B_DIRTY  0x02       /* data has been modified */
#define B_BUSY   0x04       /* buffer is in use (ref count > 0) */

/**
 * @brief Returns a buffer for the given block, reading it from disk if
 *        necessary.  The buffer is marked B_BUSY; caller must brelse() it.
 */
buf_t* bread(uint32_t dev, uint32_t blockno);

/**
 * @brief Writes a buffer to disk and marks it clean.
 */
void   bwrite(buf_t* bp);

/**
 * @brief Releases a buffer obtained via bread().  Decrements the internal
 *        reference count; the buffer may be evicted later.
 */
void   brelse(buf_t* bp);

/**
 * @brief Allocate a zeroed buffer NOT associated with any disk block.
 *        Used for creating new file blocks.  Caller must brelse().
 */
buf_t* balloc(uint32_t dev, uint32_t blockno);

/**
 * @brief Initialise the buffer cache.
 */
void   buf_init(void);

#endif
