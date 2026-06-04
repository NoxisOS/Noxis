/**
 * @file    mm/phys/pmm.c
 * @brief   Physical memory manager — bitmap frame allocator (x86-64).
 */
#include <mm/phys/pmm.h>
#include <drivers/serial.h>

void serial_write_hex64(uint64_t v);

#define PAGE_SIZE     4096ULL
#define MAX_FRAMES    (1ULL << 20)            /* up to 4 GB of frames */
#define BMP_WORDS     (MAX_FRAMES / 64)
#define RESERVED_END  (2ULL * 1024 * 1024)    /* first 2 MB reserved */

static uint64_t g_bitmap[BMP_WORDS];          /* 1 = used */
static uint8_t  g_refcount[MAX_FRAMES];       /* per-frame reference count */
static uint64_t g_nframes;
static uint64_t g_free;

static void mark_used(uint64_t f) { g_bitmap[f >> 6] |=  (1ULL << (f & 63)); }
static void mark_free(uint64_t f) { g_bitmap[f >> 6] &= ~(1ULL << (f & 63)); }
static int  is_used (uint64_t f)  { return (g_bitmap[f >> 6] >> (f & 63)) & 1; }

os_status_t pmm_init(uint64_t ram_bytes) {
    g_nframes = ram_bytes / PAGE_SIZE;
    if (g_nframes > MAX_FRAMES) g_nframes = MAX_FRAMES;

    for (uint64_t i = 0; i < BMP_WORDS; i++) g_bitmap[i] = 0;

    uint64_t reserved = RESERVED_END / PAGE_SIZE;
    for (uint64_t f = 0; f < reserved && f < g_nframes; f++) mark_used(f);
    g_free = g_nframes - reserved;

    /* Reserve the kernel heap region [16 MB, 48 MB) so the PMM never
       hands out frames the heap uses (the heap is a fixed window). */
    uint64_t hstart = (16ULL * 1024 * 1024) / PAGE_SIZE;
    uint64_t hend   = (48ULL * 1024 * 1024) / PAGE_SIZE;
    for (uint64_t f = hstart; f < hend && f < g_nframes; f++) {
        if (!is_used(f)) { mark_used(f); g_free--; }
    }

    serial_write((const uint8_t*)"[noxis64] PMM frames="); serial_write_hex64(g_nframes);
    serial_write((const uint8_t*)" free=");                serial_write_hex64(g_free);
    serial_write((const uint8_t*)"\n");
    return OS_OK;
}

uint64_t pmm_alloc_frame(void) {
    for (uint64_t f = 0; f < g_nframes; f++) {
        if (!is_used(f)) {
            mark_used(f); g_free--;
            g_refcount[f] = 1;
            return f * PAGE_SIZE;
        }
    }
    return 0;
}

/* Decrement refcount; only physically free the frame when it hits zero. */
void pmm_free_frame(uint64_t phys) {
    uint64_t f = phys / PAGE_SIZE;
    if (f >= g_nframes || !is_used(f)) return;
    if (g_refcount[f] > 1) { g_refcount[f]--; return; }
    g_refcount[f] = 0;
    mark_free(f);
    g_free++;
}

void pmm_addref(uint64_t phys) {
    uint64_t f = phys / PAGE_SIZE;
    if (f < g_nframes && g_refcount[f] < 255) g_refcount[f]++;
}

uint8_t pmm_refcount(uint64_t phys) {
    uint64_t f = phys / PAGE_SIZE;
    return (f < g_nframes) ? g_refcount[f] : 0;
}

uint64_t pmm_free_count(void) { return g_free; }
