/**
 * @file    fs/noxfs.c
 * @brief   NoxFS — disk-backed filesystem (read/write, single dir).
 *          Format: sector 0 = TOC (magic + dirents), sectors 1+ = file data.
 * @author  Noxis Team
 * @date    2026-05-30
 */
#include <fs/noxfs/noxfs.h>
#include <fs/noxfs/buffer.h>
#include <drivers/ata.h>
#include <mm/virt/heap.h>
#include <common/types.h>

#define NOXFS_MAGIC   0x5346584Eu
#define MAX_FILES     16
#define SECTOR_SIZE   512

typedef struct __attribute__((packed)) {
    uint8_t  name[24];
    uint32_t lba;
    uint32_t size;
} noxfs_dirent_t;

static vfs_file_t _files[MAX_FILES];
static uint32_t   _count;
static uint32_t   _next_lba;  /* first free sector after all files */
static int        _ready;

/* ── helpers ────────────────────────────────────────────────── */

static int _streq(const uint8_t* a, const uint8_t* b) {
    uint32_t i = 0;
    while (a[i] && b[i]) { if (a[i] != b[i]) return 0; i++; }
    return a[i] == b[i];
}

/* Write the TOC (sector 0) back to disk. */
static os_status_t _write_toc(void) {
    buf_t* bp = bread(BUF_DEV_ATA, 0);
    if (!bp) return OS_ERR_IO;

    uint32_t* hdr = (uint32_t*)bp->data;
    hdr[0] = NOXFS_MAGIC;
    hdr[1] = _count;

    noxfs_dirent_t* entries = (noxfs_dirent_t*)(bp->data + 8);
    for (uint32_t i = 0; i < _count; i++) {
        for (uint32_t j = 0; j < 24; j++)
            entries[i].name[j] = _files[i].name[j];
        entries[i].lba  = _files[i].lba;
        entries[i].size = _files[i].size;
    }

    bp->flags |= B_DIRTY;
    bwrite(bp);
    brelse(bp);
    return OS_OK;
}

/* Ensure a file's heap buffer can hold at least `needed` bytes.
   Reallocates (kmalloc → memcpy → kfree) if necessary. */
static os_status_t _ensure_capacity(vfs_file_t* f, uint32_t needed) {
    if (f->capacity >= needed) return OS_OK;

    uint32_t new_cap = (needed + SECTOR_SIZE - 1) & ~(SECTOR_SIZE - 1);
    uint8_t* new_buf = (uint8_t*)kmalloc(new_cap);
    if (!new_buf) return OS_ERR_OOM;

    for (uint32_t i = 0; i < f->size; i++)
        new_buf[i] = f->data[i];
    for (uint32_t i = f->size; i < new_cap; i++)
        new_buf[i] = 0;

    if (f->data) kfree(f->data);
    f->data     = new_buf;
    f->capacity = new_cap;
    return OS_OK;
}

/* ── public ─────────────────────────────────────────────────── */

os_status_t noxfs_init(void) {
    _ready   = 0;
    _count   = 0;
    buf_init();

    buf_t* bp = bread(BUF_DEV_ATA, 0);
    if (!bp) {
        /* Try ata_read as fallback */
        uint16_t sector0[SECTOR_SIZE / 2];
        if (ata_read(ATA_PRIMARY, ATA_MASTER, 0, 1, sector0) != OS_OK)
            return OS_ERR_IO;
        uint32_t* hdr = (uint32_t*)sector0;
        if (hdr[0] != NOXFS_MAGIC) return OS_ERR_NOT_FOUND;

        uint32_t n = hdr[1];
        if (n > MAX_FILES) n = MAX_FILES;

        noxfs_dirent_t* entries = (noxfs_dirent_t*)((uint8_t*)sector0 + 8);

        _next_lba = 1;
        for (uint32_t i = 0; i < n; i++) {
            noxfs_dirent_t* d = &entries[i];
            uint32_t sectors = (d->size + SECTOR_SIZE - 1) / SECTOR_SIZE;
            if (sectors == 0) sectors = 1;

            uint8_t* buf = (uint8_t*)kmalloc(sectors * SECTOR_SIZE);
            if (!buf) return OS_ERR_OOM;

            if (ata_read(ATA_PRIMARY, ATA_MASTER, d->lba,
                         (uint8_t)sectors, (uint16_t*)buf) != OS_OK) {
                kfree(buf); return OS_ERR_IO;
            }

            uint8_t* name = (uint8_t*)kmalloc(25);
            if (!name) { kfree(buf); return OS_ERR_OOM; }
            for (uint32_t j = 0; j < 24; j++) name[j] = d->name[j];
            name[24] = 0;

            _files[i].name     = name;
            _files[i].data     = buf;
            _files[i].size     = d->size;
            _files[i].lba      = d->lba;
            _files[i].capacity = sectors * SECTOR_SIZE;

            uint32_t end = d->lba + sectors;
            if (end > _next_lba) _next_lba = end;
        }
        _count = n;
        _ready = 1;
        return OS_OK;
    }

    /* Buffer cache path */
    uint32_t* hdr = (uint32_t*)bp->data;
    if (hdr[0] != NOXFS_MAGIC) { brelse(bp); return OS_ERR_NOT_FOUND; }

    uint32_t n = hdr[1];
    if (n > MAX_FILES) n = MAX_FILES;

    noxfs_dirent_t* entries = (noxfs_dirent_t*)(bp->data + 8);

    _next_lba = 1;
    for (uint32_t i = 0; i < n; i++) {
        noxfs_dirent_t* d = &entries[i];
        uint32_t size    = d->size;
        uint32_t sectors = (size + SECTOR_SIZE - 1) / SECTOR_SIZE;
        if (sectors == 0) sectors = 1;

        uint8_t* buf = (uint8_t*)kmalloc(sectors * SECTOR_SIZE);
        if (!buf) { brelse(bp); return OS_ERR_OOM; }

        /* Read file data via buffer cache */
        for (uint32_t s = 0; s < sectors; s++) {
            buf_t* fbp = bread(BUF_DEV_ATA, d->lba + s);
            if (!fbp) { kfree(buf); brelse(bp); return OS_ERR_IO; }
            uint32_t copy = SECTOR_SIZE;
            if (s == sectors - 1 && (size % SECTOR_SIZE))
                copy = size % SECTOR_SIZE;
            for (uint32_t b = 0; b < copy; b++)
                buf[s * SECTOR_SIZE + b] = fbp->data[b];
            brelse(fbp);
        }

        uint8_t* name = (uint8_t*)kmalloc(25);
        if (!name) { kfree(buf); brelse(bp); return OS_ERR_OOM; }
        for (uint32_t j = 0; j < 24; j++) name[j] = d->name[j];
        name[24] = 0;

        _files[i].name     = name;
        _files[i].data     = buf;
        _files[i].size     = size;
        _files[i].lba      = d->lba;
        _files[i].capacity = sectors * SECTOR_SIZE;

        uint32_t end = d->lba + sectors;
        if (end > _next_lba) _next_lba = end;
    }

    brelse(bp);
    _count = n;
    _ready = 1;
    return OS_OK;
}

uint32_t noxfs_count(void) { return _ready ? _count : 0; }

vfs_file_t* noxfs_entry(uint32_t i) {
    if (!_ready || i >= _count) return (vfs_file_t*)0;
    return &_files[i];
}

vfs_file_t* noxfs_lookup(const uint8_t* name) {
    if (!_ready) return (vfs_file_t*)0;
    for (uint32_t i = 0; i < _count; i++) {
        if (_streq(_files[i].name, name)) return &_files[i];
    }
    return (vfs_file_t*)0;
}

int32_t noxfs_write(vfs_file_t* f, uint32_t offset,
                    const uint8_t* data, uint32_t len) {
    if (!f || !data || len == 0) return 0;

    uint32_t needed = offset + len;
    if (_ensure_capacity(f, needed) != OS_OK) return -1;

    /* Write to heap buffer. */
    for (uint32_t i = 0; i < len; i++)
        f->data[offset + i] = data[i];
    if (needed > f->size) f->size = needed;

    /* Write dirty sectors through to disk via buffer cache. */
    uint32_t start_sector = offset / SECTOR_SIZE;
    uint32_t end_sector   = (needed + SECTOR_SIZE - 1) / SECTOR_SIZE;
    for (uint32_t s = start_sector; s < end_sector; s++) {
        buf_t* bp = bread(BUF_DEV_ATA, f->lba + s);
        if (!bp) continue;
        uint32_t base = s * SECTOR_SIZE;
        for (uint32_t b = 0; b < SECTOR_SIZE; b++) {
            if (base + b >= f->size) break;
            bp->data[b] = f->data[base + b];
        }
        bp->flags |= B_DIRTY;
        bwrite(bp);
        brelse(bp);
    }

    /* Update directory entry on disk. */
    if (_write_toc() != OS_OK) return -1;

    return (int32_t)len;
}

vfs_file_t* noxfs_creat(const uint8_t* name) {
    if (!_ready || !name || _count >= MAX_FILES) return (vfs_file_t*)0;
    if (noxfs_lookup(name)) return (vfs_file_t*)0;

    vfs_file_t* f = &_files[_count];

    /* Allocate one sector. */
    uint8_t* buf = (uint8_t*)kmalloc(SECTOR_SIZE);
    if (!buf) return (vfs_file_t*)0;
    for (uint32_t i = 0; i < SECTOR_SIZE; i++) buf[i] = 0;

    /* Copy name. */
    uint8_t* nm = (uint8_t*)kmalloc(25);
    if (!nm) { kfree(buf); return (vfs_file_t*)0; }
    for (uint32_t j = 0; j < 24 && name[j]; j++) nm[j] = name[j];
    nm[24] = 0;

    f->name     = nm;
    f->data     = buf;
    f->size     = 0;
    f->lba      = _next_lba;
    f->capacity = SECTOR_SIZE;

    _next_lba++;
    _count++;

    if (_write_toc() != OS_OK) {
        /* Failed to write TOC — roll back in-memory state. */
        _count--;
        _next_lba--;
        kfree(nm);
        kfree(buf);
        return (vfs_file_t*)0;
    }

    buf_t* bp = balloc(BUF_DEV_ATA, f->lba);
    if (bp) { bp->flags |= B_DIRTY; bwrite(bp); brelse(bp); }

    return f;
}

void noxfs_sync(void) {
    if (!_ready) return;
    _write_toc();
}
