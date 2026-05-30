/**
 * @file    fs/noxfs.c
 * @brief   NoxFS reader — parses sector 0 TOC, loads each file into
 *          a heap buffer so the VFS layer can hand out direct pointers.
 *          See rootfs/readme for the on-disk format.
 * @author  Noxis Team
 * @date    2026-05-30
 */
#include <fs/noxfs.h>
#include <drivers/ata.h>
#include <mm/heap.h>
#include <common/types.h>

#define NOXFS_MAGIC   0x5346584Eu      /* 'NXFS' little-endian */
#define MAX_FILES     16
#define SECTOR_SIZE   512

typedef struct __attribute__((packed)) {
    uint8_t  name[24];
    uint32_t lba;
    uint32_t size;
} noxfs_dirent_t;

static vfs_file_t _files[MAX_FILES];
static uint32_t   _count;
static int        _ready;

/* ── private ────────────────────────────────────────────────── */

static int _streq(const uint8_t* a, const uint8_t* b) {
    uint32_t i = 0;
    while (a[i] && b[i]) { if (a[i] != b[i]) return 0; i++; }
    return a[i] == b[i];
}

/* ── public ─────────────────────────────────────────────────── */

os_status_t noxfs_init(void) {
    _ready = 0;
    _count = 0;

    /* Read the superblock + dirents (sector 0). ata_read takes uint16_t* */
    uint16_t sector0[SECTOR_SIZE / 2];
    if (ata_read(ATA_PRIMARY, ATA_MASTER, 0, 1, sector0) != OS_OK) {
        return OS_ERR_IO;
    }

    uint32_t* hdr = (uint32_t*)sector0;
    if (hdr[0] != NOXFS_MAGIC) return OS_ERR_NOT_FOUND;

    uint32_t n = hdr[1];
    if (n > MAX_FILES) n = MAX_FILES;

    noxfs_dirent_t* entries = (noxfs_dirent_t*)((uint8_t*)sector0 + 8);

    for (uint32_t i = 0; i < n; i++) {
        noxfs_dirent_t* d = &entries[i];
        uint32_t size    = d->size;
        uint32_t sectors = (size + SECTOR_SIZE - 1) / SECTOR_SIZE;
        if (sectors == 0) sectors = 1;

        /* Allocate a sector-padded buffer; data lifetime = forever (kernel) */
        uint8_t* buf = (uint8_t*)kmalloc(sectors * SECTOR_SIZE);
        if (!buf) return OS_ERR_OOM;

        if (ata_read(ATA_PRIMARY, ATA_MASTER, d->lba,
                     (uint8_t)sectors, (uint16_t*)buf) != OS_OK) {
            kfree(buf);
            return OS_ERR_IO;
        }

        /* Persistent copy of the name (the sector0 stack buffer dies on return) */
        uint8_t* name = (uint8_t*)kmalloc(25);
        if (!name) { kfree(buf); return OS_ERR_OOM; }
        for (uint32_t j = 0; j < 24; j++) name[j] = d->name[j];
        name[24] = 0;

        _files[i].name = name;
        _files[i].data = buf;
        _files[i].size = size;
    }

    _count = n;
    _ready = 1;
    return OS_OK;
}

uint32_t noxfs_count(void) {
    return _ready ? _count : 0;
}

const vfs_file_t* noxfs_entry(uint32_t i) {
    if (!_ready || i >= _count) return (const vfs_file_t*)0;
    return &_files[i];
}

const vfs_file_t* noxfs_lookup(const uint8_t* name) {
    if (!_ready) return (const vfs_file_t*)0;
    for (uint32_t i = 0; i < _count; i++) {
        if (_streq(_files[i].name, name)) return &_files[i];
    }
    return (const vfs_file_t*)0;
}
