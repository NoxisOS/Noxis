/**
 * @file    mm/pmm.c
 * @brief   Physical Memory Manager — bitmap operations on 4 KB frames
 * @author  Noxis Team
 * @date    2026-05-29
 */
#include <mm/phys/pmm.h>
#include <common/types.h>

/* ── file-scope state ──────────────────────────────────────── */
static uint8_t*  g_bitmap = (uint8_t*)PMM_BITMAP_START;
static uint32_t  g_total_frames;
static uint32_t  g_free_count;

/* ── per-frame reference counts (for copy-on-write) ──────────
   Sized for 128 MB of RAM (32768 frames = 32 KB).  Frames beyond
   this range are never CoW-shared, so they need no refcount.
   A refcount of 0 means "tracked but unreferenced" or "not yet
   touched"; alloc sets it to 1, ref_inc bumps it, free decrements
   and only releases the frame when it reaches 0.                 */
#define PMM_REFCOUNT_FRAMES  (128u * 1024u * 1024u / PMM_FRAME_SIZE)
static uint8_t g_refcount[PMM_REFCOUNT_FRAMES];

static inline int _rc_tracked(uint32_t idx) {
    return idx < PMM_REFCOUNT_FRAMES;
}

/* ── private functions ─────────────────────────────────────── */

static void _bitmap_set(uint32_t frame) {
    g_bitmap[frame / 8] |= (uint8_t)(1 << (frame % 8));
}

static void _bitmap_clear(uint32_t frame) {
    g_bitmap[frame / 8] &= (uint8_t)(~(1 << (frame % 8)));
}

static bool_t _bitmap_test(uint32_t frame) {
    return (g_bitmap[frame / 8] & (uint8_t)(1 << (frame % 8))) ? TRUE : FALSE;
}

/**
 * @brief Marks a contiguous range of frames as used
 */
static void _mark_used(uint32_t start, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        _bitmap_set(start + i);
    }
    g_free_count -= count;
}

/* ── public functions ──────────────────────────────────────── */

os_status_t pmm_init(uint32_t total_memory) {
    uint32_t total_frames = total_memory / PMM_FRAME_SIZE;
    if (total_frames > PMM_TOTAL_FRAMES) {
        total_frames = PMM_TOTAL_FRAMES;
    }
    g_total_frames = total_frames;
    g_free_count  = total_frames;

    /* Zero the bitmap */
    for (uint32_t i = 0; i < PMM_BITMAP_SIZE; i++) {
        g_bitmap[i] = 0;
    }

    /* Mark BIOS + kernel image + early paging structures as used.
       kernel_entry.asm hardcodes PD=0x400000, PT0=0x401000, PTK=0x402000;
       reserving through 0x500000 leaves headroom for future static tables. */
    uint32_t kernel_end_frames = 0x00500000 / PMM_FRAME_SIZE; /* 1280 frames = 5 MB */
    _mark_used(0, kernel_end_frames);

    /* Mark the bitmap itself as used */
    uint32_t bitmap_start_frame = PMM_BITMAP_START / PMM_FRAME_SIZE;
    uint32_t bitmap_frames      = PMM_BITMAP_SIZE / PMM_FRAME_SIZE;
    _mark_used(bitmap_start_frame, bitmap_frames);

    /* Mark frames beyond total_memory as used in the bitmap (defensive —
       pmm_alloc_frame only scans up to g_total_frames anyway).
       NOTE: do NOT subtract these from g_free_count.  g_free_count was
       initialised to total_frames, so it only ever accounts for frames
       within [0, total_frames); the frames beyond it were never counted
       as free.  Subtracting them here underflowed the counter. */
    if (total_frames < PMM_TOTAL_FRAMES) {
        for (uint32_t i = total_frames; i < PMM_TOTAL_FRAMES; i++) {
            _bitmap_set(i);
        }
    }

    return OS_OK;
}

os_status_t pmm_alloc_frame(uint32_t* out) {
    if (!out) return OS_ERR_NULL;
    if (g_free_count == 0) return OS_ERR_OOM;

    /* Scan bitmap for first free frame */
    for (uint32_t i = 0; i < g_total_frames; i++) {
        if (!_bitmap_test(i)) {
            _bitmap_set(i);
            g_free_count--;
            if (_rc_tracked(i)) g_refcount[i] = 1;   /* one owner */
            *out = i * PMM_FRAME_SIZE;
            return OS_OK;
        }
    }

    return OS_ERR_OOM;
}

os_status_t pmm_free_frame(uint32_t frame) {
    uint32_t idx = frame / PMM_FRAME_SIZE;
    if (idx >= g_total_frames) return OS_ERR_INVALID;
    if (!_bitmap_test(idx)) return OS_ERR_INVALID;

    /* Reference-counted release: a CoW-shared frame stays mapped
       until its last owner frees it.                              */
    if (_rc_tracked(idx) && g_refcount[idx] > 1) {
        g_refcount[idx]--;
        return OS_OK;      /* still referenced — not actually freed */
    }

    if (_rc_tracked(idx)) g_refcount[idx] = 0;
    _bitmap_clear(idx);
    g_free_count++;
    return OS_OK;
}

/* ── Reference-count API (for copy-on-write) ─────────────────── */

void pmm_ref_inc(uint32_t frame) {
    uint32_t idx = frame / PMM_FRAME_SIZE;
    if (_rc_tracked(idx) && g_refcount[idx] < 0xFF) g_refcount[idx]++;
}

uint32_t pmm_ref_count(uint32_t frame) {
    uint32_t idx = frame / PMM_FRAME_SIZE;
    return _rc_tracked(idx) ? g_refcount[idx] : 1;
}

uint32_t pmm_frame_used(uint32_t idx) {
    if (idx >= g_total_frames) return 0;
    return _bitmap_test(idx) ? 1 : 0;
}

uint32_t pmm_get_free_count(void) {
    return g_free_count;
}

uint32_t pmm_get_total_frames(void) {
    return g_total_frames;
}
