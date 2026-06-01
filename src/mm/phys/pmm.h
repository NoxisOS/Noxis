/**
 * @file    mm/pmm.h
 * @brief   Physical Memory Manager — bitmap-based frame allocator
 * @author  Noxis Team
 * @date    2026-05-29
 */
#ifndef MM_PMM_H
#define MM_PMM_H

#include <common/types.h>
#include <common/status.h>

/* ── constants ─────────────────────────────────────────────── */
#define PMM_FRAME_SIZE      0x1000       /* 4 KB per frame */
#define PMM_BITMAP_START    0x00200000   /* Physical addr of bitmap */
#define PMM_BITMAP_SIZE     0x00020000   /* 128 KB → 1M frames */
#define PMM_TOTAL_FRAMES    0x00100000   /* 1,048,576 frames = 4 GB */

/* ── public functions ──────────────────────────────────────── */

/**
 * @brief Initializes the PMM bitmap and marks kernel regions as used
 * @param total_memory  Total usable RAM in bytes (e.g., 128 MB)
 * @return OS_OK on success
 */
os_status_t pmm_init(uint32_t total_memory);

/**
 * @brief Allocates a single physical frame (4 KB)
 * @param out  Output: physical address of the allocated frame
 * @return OS_OK on success, OS_ERR_OOM if no free frames
 */
os_status_t pmm_alloc_frame(uint32_t* out);

/**
 * @brief Frees a physical frame
 * @param frame  Physical address of the frame to free
 * @return OS_OK on success, OS_ERR_INVALID if already free
 */
os_status_t pmm_free_frame(uint32_t frame);

/**
 * @brief Increments the reference count of a frame (copy-on-write share).
 */
void pmm_ref_inc(uint32_t frame);

/**
 * @brief Returns the current reference count of a frame (1 if untracked).
 */
uint32_t pmm_ref_count(uint32_t frame);

/**
 * @brief Tests whether a frame (by index) is currently allocated.
 * @return 1 if used, 0 if free or out of range.
 */
uint32_t pmm_frame_used(uint32_t idx);

/**
 * @brief Returns the number of free frames
 * @return Free frame count
 */
uint32_t pmm_get_free_count(void);

/**
 * @brief Returns the total number of managed frames
 * @return Total frame count
 */
uint32_t pmm_get_total_frames(void);

#endif /* MM_PMM_H */
