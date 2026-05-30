/**
 * @file    drivers/block/block.h
 * @brief   Generic block device layer (Linux ll_rw_blk.c analogue).
 *
 *  Sits between the buffer cache and the low-level disk drivers (ATA).
 *  Drivers register a `block_device_t` describing a sector-addressable
 *  device and a `transfer` callback.  Higher layers issue I/O through
 *  blk_rw(), which builds a request, inserts it into the device's queue
 *  using an elevator (C-SCAN) sort, then drains the queue synchronously
 *  (Noxis ATA is PIO, so there is no async completion IRQ — the elevator
 *  is real but requests complete in place).
 *
 * @author  Noxis Team
 * @date    2026-05-30
 */
#ifndef DRIVERS_BLOCK_BLOCK_H
#define DRIVERS_BLOCK_BLOCK_H

#include <common/types.h>
#include <common/status.h>

#define BLK_SECTOR_SIZE   512u
#define BLK_MAX_DEVICES   4
#define BLK_NR_REQUEST    16     /* request slots in the shared pool */

#define BLK_READ   0
#define BLK_WRITE  1

struct block_device;

/**
 * @brief Low-level sector transfer implemented by a driver.
 *        Moves `count` sectors starting at `lba` between the device and
 *        `buf`.  `write` is BLK_READ or BLK_WRITE.
 * @return OS_OK on success.
 */
typedef os_status_t (*blk_transfer_fn)(struct block_device* dev,
                                       uint32_t lba, uint32_t count,
                                       void* buf, int write);

/** A device registered with the block layer. */
typedef struct block_device {
    const char*     name;       /* e.g. "ata0" */
    uint32_t        sectors;    /* total addressable sectors (0 = unknown) */
    blk_transfer_fn transfer;   /* driver entry point */
    void*           drvdata;    /* driver private data */
    int             registered;
} block_device_t;

/** A queued I/O request. */
typedef struct blk_request {
    int                 dev;     /* device id */
    int                 write;   /* BLK_READ / BLK_WRITE */
    uint32_t            lba;
    uint32_t            count;
    void*               buf;
    os_status_t         status;  /* set on completion */
    struct blk_request* next;
    int                 in_use;
} blk_request_t;

/** Initialise the request pool and device registry. */
void            blk_init(void);

/**
 * @brief Register a block device driver.
 * @return device id (>= 0) on success, -1 if the registry is full.
 */
int             blk_register(block_device_t* dev);

/** @return the device for `devid`, or NULL if out of range / unregistered. */
block_device_t* blk_get(int devid);

/** @return number of registered devices. */
int             blk_device_count(void);

/**
 * @brief Issue synchronous, elevator-scheduled block I/O.
 * @param devid  registered device id
 * @param lba    starting sector
 * @param count  sector count (>= 1)
 * @param buf    data buffer (count * 512 bytes)
 * @param write  BLK_READ or BLK_WRITE
 * @return OS_OK on success.
 */
os_status_t     blk_rw(int devid, uint32_t lba, uint32_t count,
                       void* buf, int write);

/** @return total number of requests served since boot (diagnostics). */
uint32_t        blk_requests_served(void);

#endif /* DRIVERS_BLOCK_BLOCK_H */
