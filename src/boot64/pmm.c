/**
 * @file    src/boot64/pmm.c
 * @brief   Physical memory manager — bitmap frame allocator (64-bit).
 *
 * For now we assume a fixed RAM size (QEMU's default 128 MB); a later
 * phase will parse the BIOS E820 map passed by the boot loader.
 */
#include "types.h"

void serial_write(const char* s);
void serial_hex(uint64_t v);

#define PAGE_SIZE   4096ULL
#define RAM_BYTES   (128ULL * 1024 * 1024)        /* assume 128 MB */
#define NFRAMES     (RAM_BYTES / PAGE_SIZE)        /* 32768 frames */
#define BMP_WORDS   (NFRAMES / 64)                 /* 64 bits per word */

/* Everything below this is reserved (kernel, page tables, stack, low mem). */
#define RESERVED_END  (2ULL * 1024 * 1024)         /* first 2 MB */

static uint64_t g_bitmap[BMP_WORDS];               /* 1 = used */
static uint64_t g_free_frames;

static void mark_used(uint64_t f) { g_bitmap[f >> 6] |=  (1ULL << (f & 63)); }
static void mark_free(uint64_t f) { g_bitmap[f >> 6] &= ~(1ULL << (f & 63)); }
static int  is_used (uint64_t f)  { return (g_bitmap[f >> 6] >> (f & 63)) & 1; }

void pmm_init(void) {
    for (uint64_t i = 0; i < BMP_WORDS; i++) g_bitmap[i] = 0;

    /* Reserve the first 2 MB. */
    uint64_t reserved = RESERVED_END / PAGE_SIZE;
    for (uint64_t f = 0; f < reserved; f++) mark_used(f);

    g_free_frames = NFRAMES - reserved;

    serial_write("[noxis64] PMM: frames="); serial_hex(NFRAMES);
    serial_write(" free=");                  serial_hex(g_free_frames);
    serial_write("\n");
}

/* Returns a zeroed-physical-address frame, or 0 on out-of-memory. */
uint64_t pmm_alloc_frame(void) {
    for (uint64_t f = 0; f < NFRAMES; f++) {
        if (!is_used(f)) {
            mark_used(f);
            g_free_frames--;
            return f * PAGE_SIZE;
        }
    }
    return 0;
}

void pmm_free_frame(uint64_t phys) {
    uint64_t f = phys / PAGE_SIZE;
    if (f < NFRAMES && is_used(f)) {
        mark_free(f);
        g_free_frames++;
    }
}

uint64_t pmm_free_count(void) { return g_free_frames; }
