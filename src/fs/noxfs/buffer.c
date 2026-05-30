/**
 * @file    fs/buffer.c
 * @brief   Buffer cache — LRU hash-table disk block cache.
 *
 *          16 buffers × 512 B = 8 KB total cache.  The hash table and
 *          LRU list are file-scope; all access protected by cli/sti
 *          (single-CPU, no SMP).
 * @author  Noxis Team
 * @date    2026-05-30
 */
#include <fs/noxfs/buffer.h>
#include <drivers/block/block.h>
#include <mm/virt/heap.h>
#include <kernel/hal/ports.h>
#include <common/types.h>

#define NBUF        16
#define HASH_SIZE   8

static buf_t  g_buffers[NBUF];
static buf_t* g_hash[HASH_SIZE];
static buf_t  g_lru_head_sentinel;
static buf_t  g_lru_tail_sentinel;

/* ── helpers ────────────────────────────────────────────────── */

static uint32_t _hash(uint32_t dev, uint32_t blockno) {
    return (dev * 31 + blockno) % HASH_SIZE;
}

static void _lru_remove(buf_t* bp) {
    if (!bp->lru_prev || !bp->lru_next) return;
    bp->lru_prev->lru_next = bp->lru_next;
    bp->lru_next->lru_prev = bp->lru_prev;
    bp->lru_prev = bp->lru_next = (buf_t*)0;
}

static void _lru_insert_head(buf_t* bp) {
    bp->lru_next = g_lru_head_sentinel.lru_next;
    bp->lru_prev = &g_lru_head_sentinel;
    g_lru_head_sentinel.lru_next->lru_prev = bp;
    g_lru_head_sentinel.lru_next = bp;
}

static void _hash_insert(buf_t* bp) {
    uint32_t h = _hash(bp->dev, bp->blockno);
    bp->hash_next = g_hash[h];
    g_hash[h] = bp;
}

static void _hash_remove(buf_t* bp) {
    uint32_t h = _hash(bp->dev, bp->blockno);
    buf_t** p = &g_hash[h];
    while (*p) {
        if (*p == bp) { *p = bp->hash_next; return; }
        p = &(*p)->hash_next;
    }
}

static buf_t* _hash_lookup(uint32_t dev, uint32_t blockno) {
    uint32_t h = _hash(dev, blockno);
    buf_t* p = g_hash[h];
    while (p) {
        if (p->dev == dev && p->blockno == blockno) return p;
        p = p->hash_next;
    }
    return (buf_t*)0;
}

/* Find a free buffer (not B_BUSY, not B_DIRTY, LRU tail first). */
static buf_t* _evict(void) {
    buf_t* p = g_lru_tail_sentinel.lru_prev;
    while (p && p != &g_lru_head_sentinel) {
        if (!(p->flags & B_BUSY) && !(p->flags & B_DIRTY)) return p;
        p = p->lru_prev;
    }
    p = g_lru_tail_sentinel.lru_prev;
    while (p && p != &g_lru_head_sentinel) {
        if (!(p->flags & B_BUSY)) { bwrite(p); return p; }
        p = p->lru_prev;
    }
    return (buf_t*)0;
}

/* ── public ─────────────────────────────────────────────────── */

void buf_init(void) {
    g_lru_head_sentinel.lru_next = &g_lru_tail_sentinel;
    g_lru_head_sentinel.lru_prev = (buf_t*)0;
    g_lru_tail_sentinel.lru_prev = &g_lru_head_sentinel;
    g_lru_tail_sentinel.lru_next = (buf_t*)0;

    for (uint32_t i = 0; i < NBUF; i++) {
        g_buffers[i].flags     = 0;
        g_buffers[i].dev       = 0;
        g_buffers[i].blockno   = 0;
        g_buffers[i].hash_next = (buf_t*)0;
        /* Link into LRU list so _evict can always find a candidate. */
        g_buffers[i].lru_prev = &g_lru_head_sentinel;
        g_buffers[i].lru_next = g_lru_head_sentinel.lru_next;
        g_lru_head_sentinel.lru_next->lru_prev = &g_buffers[i];
        g_lru_head_sentinel.lru_next = &g_buffers[i];
    }
    for (uint32_t i = 0; i < HASH_SIZE; i++)
        g_hash[i] = (buf_t*)0;
}

buf_t* bread(uint32_t dev, uint32_t blockno) {
    buf_t* bp = _hash_lookup(dev, blockno);
    if (bp) {
        if (!(bp->flags & B_VALID)) {
            blk_rw((int)dev, blockno, 1, bp->data, BLK_READ);
            bp->flags |= B_VALID;
        }
        _lru_remove(bp);
        _lru_insert_head(bp);
        bp->flags |= B_BUSY;
        return bp;
    }

    bp = _evict();
    if (!bp) return (buf_t*)0;

    if (bp->flags & B_VALID)
        _hash_remove(bp);

    bp->dev      = dev;
    bp->blockno  = blockno;
    bp->flags    = 0;

    blk_rw((int)dev, blockno, 1, bp->data, BLK_READ);
    bp->flags = B_VALID | B_BUSY;

    _lru_remove(bp);       /* remove from current LRU position before reinserting */
    _hash_insert(bp);
    _lru_insert_head(bp);
    return bp;
}

void bwrite(buf_t* bp) {
    if (!bp || !(bp->flags & B_DIRTY)) return;
    if (blk_rw((int)bp->dev, bp->blockno, 1, bp->data, BLK_WRITE) != OS_OK)
        return;  /* keep B_DIRTY so caller can retry */
    bp->flags &= ~B_DIRTY;
}

void brelse(buf_t* bp) {
    if (!bp) return;
    bp->flags &= ~B_BUSY;
}

buf_t* balloc(uint32_t dev, uint32_t blockno) {
    /* Recycle any free buffer — don't read from disk. */
    buf_t* bp = _evict();
    if (!bp) {
        /* Try harder: flush dirty, non-busy buffers. */
        for (uint32_t i = 0; i < NBUF; i++) {
            if (!(g_buffers[i].flags & B_BUSY) &&
                 (g_buffers[i].flags & B_DIRTY))
                bwrite(&g_buffers[i]);
        }
        bp = _evict();
    }
    if (!bp) return (buf_t*)0;

    if (bp->flags & B_VALID)
        _hash_remove(bp);

    bp->dev      = dev;
    bp->blockno  = blockno;
    bp->flags    = B_BUSY;

    for (uint32_t i = 0; i < BUF_SIZE; i++)
        bp->data[i] = 0;

    _lru_remove(bp);       /* remove from current LRU position before reinserting */
    _hash_insert(bp);
    _lru_insert_head(bp);
    return bp;
}
