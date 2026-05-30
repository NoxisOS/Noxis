/**
 * @file    mm/pmm.c
 * @brief   Physical Memory Manager — bitmap operations on 4 KB frames
 * @author  Noxis Team
 * @date    2026-05-29
 */
#include <mm/pmm.h>
#include <common/types.h>

/* ── file-scope state ──────────────────────────────────────── */
static uint8_t*  g_bitmap = (uint8_t*)PMM_BITMAP_START;
static uint32_t  g_total_frames;
static uint32_t  g_free_count;

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

    /* Mark frames beyond total_memory as used */
    if (total_frames < PMM_TOTAL_FRAMES) {
        for (uint32_t i = total_frames; i < PMM_TOTAL_FRAMES; i++) {
            _bitmap_set(i);
        }
        g_free_count -= (PMM_TOTAL_FRAMES - total_frames);
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

    _bitmap_clear(idx);
    g_free_count++;
    return OS_OK;
}

uint32_t pmm_get_free_count(void) {
    return g_free_count;
}

uint32_t pmm_get_total_frames(void) {
    return g_total_frames;
}
