/**
 * @file    drivers/block/block.c
 * @brief   Generic block device layer — request queue + elevator scheduler.
 *
 *  Mirrors the structure of Linux 0.01's ll_rw_blk.c: a fixed pool of
 *  request slots, one queue per device, and an elevator (C-SCAN) insertion
 *  that keeps the queue sorted by ascending LBA to minimise head seeks.
 *  Because the ATA driver is PIO (no completion IRQ), blk_rw() drains the
 *  queue to completion before returning — the scheduling is real, the
 *  execution is synchronous.
 *
 * @author  Noxis Team
 * @date    2026-05-30
 */
#include <drivers/block/block.h>
#include <kernel/hal/ports.h>
#include <common/types.h>

/* Save EFLAGS + cli; restore EFLAGS.  Unlike a bare cli/sti pair this
   preserves the caller's interrupt state — critical because blk_rw runs
   during early boot (vfs_init) when interrupts are still globally disabled
   and must stay that way until kernel_main calls cpu_sti(). */
static inline uint64_t _irq_save(void) {
    uint64_t flags;
    __asm__ __volatile__("pushfq\n\t pop %0\n\t cli" : "=r"(flags) :: "memory");
    return flags;
}
static inline void _irq_restore(uint64_t flags) {
    __asm__ __volatile__("push %0\n\t popfq" :: "r"(flags) : "memory", "cc");
}

/* ── registry + request pool ────────────────────────────────── */

static block_device_t* g_devices[BLK_MAX_DEVICES];
static int             g_dev_count;

static blk_request_t   g_pool[BLK_NR_REQUEST];
static blk_request_t*  g_queue[BLK_MAX_DEVICES];  /* per-device request list */

static uint32_t        g_served;

/* ── init ───────────────────────────────────────────────────── */

void blk_init(void) {
    g_dev_count = 0;
    g_served    = 0;
    for (int i = 0; i < BLK_MAX_DEVICES; i++) {
        g_devices[i] = (block_device_t*)0;
        g_queue[i]   = (blk_request_t*)0;
    }
    for (int i = 0; i < BLK_NR_REQUEST; i++) {
        g_pool[i].in_use = 0;
        g_pool[i].next   = (blk_request_t*)0;
    }
}

int blk_register(block_device_t* dev) {
    if (!dev || !dev->transfer) return -1;
    if (g_dev_count >= BLK_MAX_DEVICES) return -1;

    int id = g_dev_count++;
    dev->registered = 1;
    g_devices[id] = dev;
    return id;
}

block_device_t* blk_get(int devid) {
    if (devid < 0 || devid >= g_dev_count) return (block_device_t*)0;
    return g_devices[devid];
}

int blk_device_count(void) { return g_dev_count; }

uint32_t blk_requests_served(void) { return g_served; }

/* ── request pool helpers ───────────────────────────────────── */

static blk_request_t* _request_alloc(void) {
    for (int i = 0; i < BLK_NR_REQUEST; i++) {
        if (!g_pool[i].in_use) {
            g_pool[i].in_use = 1;
            g_pool[i].next   = (blk_request_t*)0;
            return &g_pool[i];
        }
    }
    return (blk_request_t*)0;
}

static void _request_free(blk_request_t* r) {
    r->in_use = 0;
    r->next   = (blk_request_t*)0;
}

/* Elevator insert: keep the device queue sorted by ascending LBA so the
   disk head sweeps in one direction (C-SCAN).  Single-CPU: caller holds
   interrupts disabled. */
static void _elevator_insert(int devid, blk_request_t* req) {
    blk_request_t** pp = &g_queue[devid];
    while (*pp && (*pp)->lba <= req->lba)
        pp = &(*pp)->next;
    req->next = *pp;
    *pp = req;
}

/* Drain a device's queue, executing each request in elevator order. */
static void _run_queue(int devid) {
    block_device_t* dev = g_devices[devid];
    blk_request_t*  req;

    while ((req = g_queue[devid]) != (blk_request_t*)0) {
        g_queue[devid] = req->next;

        req->status = dev->transfer(dev, req->lba, req->count,
                                    req->buf, req->write);
        g_served++;
        /* Slot stays in_use until the issuer reads req->status and frees
           it (synchronous, single-threaded: no reuse can race here). */
    }
}

/* ── public I/O ─────────────────────────────────────────────── */

os_status_t blk_rw(int devid, uint32_t lba, uint32_t count,
                   void* buf, int write) {
    if (devid < 0 || devid >= g_dev_count) return OS_ERR_INVALID;
    if (!buf || count == 0) return OS_ERR_INVALID;

    block_device_t* dev = g_devices[devid];
    if (!dev || !dev->transfer) return OS_ERR_NOT_FOUND;

    /* Bounds check against the device size when known. */
    if (dev->sectors && (lba + count > dev->sectors))
        return OS_ERR_RANGE;

    uint64_t flags = _irq_save();
    blk_request_t* req = _request_alloc();
    if (!req) { _irq_restore(flags); return OS_ERR_BUSY; }

    req->dev    = devid;
    req->write  = write;
    req->lba    = lba;
    req->count  = count;
    req->buf    = buf;
    req->status = OS_ERR_IO;

    _elevator_insert(devid, req);
    _run_queue(devid);

    os_status_t st = req->status;   /* read before freeing the slot */
    _request_free(req);
    _irq_restore(flags);
    return st;
}
