/**
 * @file    fs/noxfs/noxfs.c
 * @brief   NoxFS v2 — inode filesystem with bitmap allocators.
 *          Phase 1: flat root directory.  Phase 2 will add subdirectories.
 *
 *   Block 0  = superblock
 *   Block 1  = block bitmap (1 bit per block)
 *   Block 2  = inode bitmap (1 bit per inode)
 *   Blocks 3+ = inode table (ceil(ino_count * 64 / 512) blocks)
 *   Blocks data_start+ = file data
 *
 * @author  Noxis Team
 * @date    2026-05-30
 */
#include <fs/noxfs/noxfs.h>
#include <fs/noxfs/buffer.h>
#include <drivers/ata.h>
#include <mm/virt/heap.h>
#include <common/types.h>

#define BITS_PER_BYTE  8
#define INODES_PER_BLK (NOXFS_BLKSZ / NOXFS_INO_SZ)

/* ── file-scope state ──────────────────────────────────────── */
static noxfs_sb_t  g_sb;
static vfs_file_t  g_files[NOXFS_MAX_FILES];
static uint32_t    g_count;
static int         g_ready;

/* ── bitmap helpers ────────────────────────────────────────── */
/* Find and set the first zero bit in a bitmap spanning `blk` blocks
   starting at `start_blk`.  Returns the bit index, or (uint32_t)-1. */
static uint32_t _bmp_alloc(uint32_t start_blk, uint32_t blk_count,
                           uint32_t max_bits) {
    for (uint32_t b = 0; b < blk_count; b++) {
        buf_t* bp = bread(BUF_DEV_ATA, start_blk + b);
        if (!bp) return (uint32_t)-1;
        for (uint32_t byte = 0; byte < NOXFS_BLKSZ; byte++) {
            if (bp->data[byte] == 0xFF) continue;
            for (uint32_t bit = 0; bit < BITS_PER_BYTE; bit++) {
                uint32_t idx = b * NOXFS_BLKSZ * BITS_PER_BYTE + byte * BITS_PER_BYTE + bit;
                if (idx >= max_bits) { brelse(bp); return (uint32_t)-1; }
                if (!(bp->data[byte] & (1u << bit))) {
                    bp->data[byte] |= (1u << bit);
                    bp->flags |= B_DIRTY;
                    bwrite(bp);
                    brelse(bp);
                    return idx;
                }
            }
        }
        brelse(bp);
    }
    return (uint32_t)-1;
}

static void _bmp_free(uint32_t start_blk, uint32_t bit) {
    uint32_t blk_off  = bit / (NOXFS_BLKSZ * BITS_PER_BYTE);
    uint32_t byte_off = (bit % (NOXFS_BLKSZ * BITS_PER_BYTE)) / BITS_PER_BYTE;
    uint32_t bit_off  = bit % BITS_PER_BYTE;

    buf_t* bp = bread(BUF_DEV_ATA, start_blk + blk_off);
    if (!bp) return;
    bp->data[byte_off] &= ~(1u << bit_off);
    bp->flags |= B_DIRTY;
    bwrite(bp);
    brelse(bp);
}

/* ── superblock ────────────────────────────────────────────── */

static os_status_t __attribute__((unused)) _sb_read(void) {
    buf_t* bp = bread(BUF_DEV_ATA, 0);

    if (!bp) {
        /* Fallback: read directly via ATA PIO */
        uint8_t raw[NOXFS_BLKSZ];
        if (ata_read(ATA_PRIMARY, ATA_MASTER, 0, 1, (uint16_t*)raw) != OS_OK)
            return OS_ERR_IO;

        uint32_t magic = *(uint32_t*)raw;
        if (magic != NOXFS_MAGIC) return OS_ERR_NOT_FOUND;

        for (uint32_t i = 0; i < sizeof(noxfs_sb_t); i++)
            ((uint8_t*)&g_sb)[i] = raw[i];

        return OS_OK;
    }

    noxfs_sb_t* sb = (noxfs_sb_t*)bp->data;
    if (sb->magic != NOXFS_MAGIC) { brelse(bp); return OS_ERR_NOT_FOUND; }

    for (uint32_t i = 0; i < sizeof(noxfs_sb_t); i++)
        ((uint8_t*)&g_sb)[i] = bp->data[i];

    brelse(bp);
    return OS_OK;
}

static os_status_t _sb_write(void) {
    buf_t* bp = bread(BUF_DEV_ATA, 0);
    if (!bp) return OS_ERR_IO;

    for (uint32_t i = 0; i < sizeof(noxfs_sb_t); i++)
        bp->data[i] = ((uint8_t*)&g_sb)[i];

    bp->flags |= B_DIRTY;
    bwrite(bp);
    brelse(bp);
    return OS_OK;
}

/* ── block allocator ───────────────────────────────────────── */

static uint32_t _balloc(void) {
    if (g_sb.free_blocks == 0) return (uint32_t)-1;
    uint32_t blk = _bmp_alloc(g_sb.blk_bmp, 1, g_sb.block_count);
    if (blk == (uint32_t)-1) return (uint32_t)-1;
    g_sb.free_blocks--;
    _sb_write();
    return blk;
}

static void __attribute__((unused)) _bfree(uint32_t blk) {
    if (blk >= g_sb.block_count) return;
    _bmp_free(g_sb.blk_bmp, blk);
    g_sb.free_blocks++;
    _sb_write();
}

/* ── inode allocator ───────────────────────────────────────── */

static uint32_t _ialloc(uint16_t mode) {
    if (g_sb.free_inodes == 0) return (uint32_t)-1;
    uint32_t ino = _bmp_alloc(g_sb.ino_bmp, 1, g_sb.inode_count);
    if (ino == (uint32_t)-1) return (uint32_t)-1;

    noxfs_inode_t ino_data;
    for (uint32_t i = 0; i < sizeof(ino_data); i++) ((uint8_t*)&ino_data)[i] = 0;
    ino_data.mode  = mode;
    ino_data.links = 1;

    uint32_t blk = g_sb.ino_tbl + ino / INODES_PER_BLK;
    uint32_t off = (ino % INODES_PER_BLK) * NOXFS_INO_SZ;

    buf_t* bp = bread(BUF_DEV_ATA, blk);
    if (!bp) { _bmp_free(g_sb.ino_bmp, ino); return (uint32_t)-1; }
    for (uint32_t i = 0; i < NOXFS_INO_SZ; i++)
        bp->data[off + i] = ((uint8_t*)&ino_data)[i];
    bp->flags |= B_DIRTY;
    bwrite(bp);
    brelse(bp);

    g_sb.free_inodes--;
    _sb_write();

    return ino;
}

/* ── inode read/write ──────────────────────────────────────── */

static os_status_t _iread(uint32_t ino, noxfs_inode_t* out) {
    if (ino >= g_sb.inode_count) return OS_ERR_INVALID;
    uint32_t blk = g_sb.ino_tbl + ino / INODES_PER_BLK;
    uint32_t off = (ino % INODES_PER_BLK) * NOXFS_INO_SZ;

    buf_t* bp = bread(BUF_DEV_ATA, blk);
    if (bp) {
        for (uint32_t i = 0; i < NOXFS_INO_SZ; i++)
            ((uint8_t*)out)[i] = bp->data[off + i];
        brelse(bp);
        return OS_OK;
    }

    /* Fallback: direct ATA PIO */
    uint16_t raw[NOXFS_BLKSZ / 2];
    if (ata_read(ATA_PRIMARY, ATA_MASTER, blk, 1, raw) != OS_OK)
        return OS_ERR_IO;
    for (uint32_t i = 0; i < NOXFS_INO_SZ; i++)
        ((uint8_t*)out)[i] = ((uint8_t*)raw)[off + i];
    return OS_OK;
}

static os_status_t _iwrite(uint32_t ino, const noxfs_inode_t* in) {
    if (ino >= g_sb.inode_count) return OS_ERR_INVALID;
    uint32_t blk = g_sb.ino_tbl + ino / INODES_PER_BLK;
    uint32_t off = (ino % INODES_PER_BLK) * NOXFS_INO_SZ;

    buf_t* bp = bread(BUF_DEV_ATA, blk);
    if (!bp) return OS_ERR_IO;
    for (uint32_t i = 0; i < NOXFS_INO_SZ; i++)
        bp->data[off + i] = ((uint8_t*)in)[i];
    bp->flags |= B_DIRTY;
    bwrite(bp);
    brelse(bp);
    return OS_OK;
}

/* ── block mapping ─────────────────────────────────────────── */

static uint32_t _iget_block(noxfs_inode_t* ino, uint32_t logical,
                             int allocate) {
    if (logical < NOXFS_DIRECT) {
        if (allocate && ino->blocks[logical] == 0) {
            uint32_t blk = _balloc();
            if (blk == (uint32_t)-1) return 0;
            ino->blocks[logical] = blk;
        }
        return ino->blocks[logical];
    }

    logical -= NOXFS_DIRECT;
    if (logical >= NOXFS_INDIRECT) return 0;

    if (allocate && ino->indirect == 0) {
        uint32_t blk = _balloc();
        if (blk == (uint32_t)-1) return 0;
        ino->indirect = blk;

        buf_t* bp = bread(BUF_DEV_ATA, blk);
        if (!bp) return 0;
        for (uint32_t i = 0; i < NOXFS_BLKSZ; i++) bp->data[i] = 0;
        bp->flags |= B_DIRTY;
        bwrite(bp);
        brelse(bp);
    }

    if (ino->indirect == 0) return 0;

    buf_t* bp = bread(BUF_DEV_ATA, ino->indirect);
    if (!bp) return 0;
    uint32_t* ptrs = (uint32_t*)bp->data;

    if (allocate && ptrs[logical] == 0) {
        uint32_t blk = _balloc();
        if (blk == (uint32_t)-1) { brelse(bp); return 0; }
        ptrs[logical] = blk;
        bp->flags |= B_DIRTY;
        bwrite(bp);
    }
    uint32_t result = ptrs[logical];
    brelse(bp);
    return result;
}

/* ── file data load / store ────────────────────────────────── */

static uint8_t* _load_data(uint32_t ino, uint32_t* size_out) {
    noxfs_inode_t in;
    if (_iread(ino, &in) != OS_OK) return (uint8_t*)0;
    *size_out = in.size;
    if (in.size == 0) {
        uint8_t* buf = (uint8_t*)kmalloc(1);
        if (buf) buf[0] = 0;
        return buf;
    }

    uint32_t alloc = (in.size + NOXFS_BLKSZ - 1) & ~(NOXFS_BLKSZ - 1);
    uint8_t* buf = (uint8_t*)kmalloc(alloc);
    if (!buf) return (uint8_t*)0;

    for (uint32_t s = 0; s * NOXFS_BLKSZ < in.size; s++) {
        /* Use _iget_block(allocate=0) so both direct AND indirect blocks
           are resolved correctly.  ELF files like ctest.elf exceed 5120 bytes
           (10 direct × 512) and land in the indirect block range. */
        uint32_t blk = _iget_block(&in, s, 0);

        if (blk == 0) continue;

        /* Try bread first, fall back to direct ATA PIO */
        buf_t* bp = bread(BUF_DEV_ATA, blk);
        if (bp) {
            uint32_t copy = NOXFS_BLKSZ;
            if (s * NOXFS_BLKSZ + copy > in.size)
                copy = in.size - s * NOXFS_BLKSZ;
            for (uint32_t b = 0; b < copy; b++)
                buf[s * NOXFS_BLKSZ + b] = bp->data[b];
            brelse(bp);
        } else {
            /* Direct ATA fallback */
            uint16_t raw[NOXFS_BLKSZ / 2];
            if (ata_read(ATA_PRIMARY, ATA_MASTER, blk, 1, raw) != OS_OK) {
                kfree(buf);
                return (uint8_t*)0;
            }
            uint32_t copy = NOXFS_BLKSZ;
            if (s * NOXFS_BLKSZ + copy > in.size)
                copy = in.size - s * NOXFS_BLKSZ;
            for (uint32_t b = 0; b < copy; b++)
                buf[s * NOXFS_BLKSZ + b] = ((uint8_t*)raw)[b];
        }
    }
    return buf;
}

static os_status_t _store_data(uint32_t ino, const uint8_t* data,
                                uint32_t size) {
    noxfs_inode_t in;
    if (_iread(ino, &in) != OS_OK) return OS_ERR_IO;
    uint32_t new_blks = (size + NOXFS_BLKSZ - 1) / NOXFS_BLKSZ;

    in.size = size;
    if (size == 0) new_blks = 0;

    for (uint32_t s = 0; s < new_blks; s++) {
        uint32_t blk = _iget_block(&in, s, 1);
        if (blk == 0) return OS_ERR_OOM;
        buf_t* bp = bread(BUF_DEV_ATA, blk);
        if (!bp) return OS_ERR_IO;
        uint32_t copy = NOXFS_BLKSZ;
        if (s * NOXFS_BLKSZ + copy > size)
            copy = size - s * NOXFS_BLKSZ;
        for (uint32_t b = 0; b < copy; b++)
            bp->data[b] = data[s * NOXFS_BLKSZ + b];
        bp->flags |= B_DIRTY;
        bwrite(bp);
        brelse(bp);
    }

    if (_iwrite(ino, &in) != OS_OK) return OS_ERR_IO;
    return OS_OK;
}

/* ── directory scan (flat root dir only, Phase 1) ──────────── */

static os_status_t _dir_scan(uint32_t dir_ino) {
    noxfs_inode_t dir;
    if (_iread(dir_ino, &dir) != OS_OK) return OS_ERR_IO;
    if (!(dir.mode & NOXFS_INO_DIR)) return OS_ERR_INVALID;

    uint32_t dir_sz = dir.size;
    uint8_t* dir_data = _load_data(dir_ino, &dir_sz);
    if (!dir_data) return OS_ERR_OOM;

    noxfs_dirent_t* entries = (noxfs_dirent_t*)dir_data;
    uint32_t entry_count = dir_sz / sizeof(noxfs_dirent_t);

    g_count = 0;
    for (uint32_t i = 0; i < entry_count && g_count < NOXFS_MAX_FILES; i++) {
        if (entries[i].inode == 0) continue;
        if (entries[i].file_type != NOXFS_FT_FILE) continue;

        vfs_file_t* f = &g_files[g_count];

        uint32_t name_len = entries[i].name_len;
        if (name_len > 31) name_len = 31;
        for (uint32_t j = 0; j < name_len; j++)
            f->name[j] = (uint8_t)entries[i].name[j];
        f->name[name_len] = 0;

        f->data = _load_data(entries[i].inode, &f->size);
        if (!f->data) continue;

        f->inode    = entries[i].inode;
        f->capacity = (f->size + NOXFS_BLKSZ - 1) & ~(NOXFS_BLKSZ - 1);
        if (f->capacity == 0) f->capacity = NOXFS_BLKSZ;

        g_count++;
    }

    kfree(dir_data);
    return OS_OK;
}

/* ── healper ───────────────────────────────────────────────── */

static int _streq(const uint8_t* a, const uint8_t* b) {
    uint32_t i = 0;
    while (a[i] && b[i]) { if (a[i] != b[i]) return 0; i++; }
    return a[i] == b[i];
}

/* ── public API ────────────────────────────────────────────── */

os_status_t noxfs_init(void) {
    g_ready = 0;
    g_count = 0;
    buf_init();

    /* Read superblock directly — bypass buffer cache for boot reliability. */
    {
        uint8_t raw[NOXFS_BLKSZ];
        if (ata_read(ATA_PRIMARY, ATA_MASTER, 0, 1, (uint16_t*)raw) != OS_OK)
            return OS_ERR_NOT_FOUND;

        uint32_t magic = *(uint32_t*)raw;
        if (magic != NOXFS_MAGIC) return OS_ERR_NOT_FOUND;

        for (uint32_t i = 0; i < sizeof(noxfs_sb_t); i++)
            ((uint8_t*)&g_sb)[i] = raw[i];
    }

    if (_dir_scan(g_sb.root_ino) != OS_OK) return OS_ERR_IO;

    g_ready = 1;
    return OS_OK;
}

uint32_t noxfs_count(void) { return g_ready ? g_count : 0; }

vfs_file_t* noxfs_entry(uint32_t i) {
    if (!g_ready || i >= g_count) return (vfs_file_t*)0;
    return &g_files[i];
}

vfs_file_t* noxfs_lookup(const uint8_t* name) {
    if (!g_ready) return (vfs_file_t*)0;

    /* First check the in-memory cache (flat list from _dir_scan). */
    for (uint32_t i = 0; i < g_count; i++) {
        if (_streq(g_files[i].name, name))
            return &g_files[i];
    }

    /* Try resolving as a path (handles subdirectories). */
    uint32_t ino = noxfs_resolve(g_sb.root_ino, name);
    if (ino == (uint32_t)-1 || ino == g_sb.root_ino) return (vfs_file_t*)0;

    noxfs_inode_t in;
    if (_iread(ino, &in) != OS_OK) return (vfs_file_t*)0;
    if (!(in.mode & NOXFS_INO_FILE)) return (vfs_file_t*)0;

    if (g_count >= NOXFS_MAX_FILES) return (vfs_file_t*)0;
    vfs_file_t* f = &g_files[g_count];

    /* Extract basename from path */
    const uint8_t* base = name;
    for (const uint8_t* p = name; *p; p++)
        if (*p == '/') base = p + 1;

    uint32_t j;
    for (j = 0; j < 31 && base[j]; j++) f->name[j] = base[j];
    f->name[j] = 0;

    f->data = _load_data(ino, &f->size);
    if (!f->data) return (vfs_file_t*)0;

    f->inode    = ino;
    f->capacity = (f->size + NOXFS_BLKSZ - 1) & ~(NOXFS_BLKSZ - 1);
    if (f->capacity == 0) f->capacity = NOXFS_BLKSZ;
    g_count++;

    return f;
}

static os_status_t _ensure_capacity(vfs_file_t* f, uint32_t needed) {
    if (f->capacity >= needed) return OS_OK;
    uint32_t new_cap = (needed + NOXFS_BLKSZ - 1) & ~(NOXFS_BLKSZ - 1);
    uint8_t* new_buf = (uint8_t*)kmalloc(new_cap);
    if (!new_buf) return OS_ERR_OOM;
    for (uint32_t i = 0; i < f->size; i++) new_buf[i] = f->data[i];
    for (uint32_t i = f->size; i < new_cap; i++) new_buf[i] = 0;
    if (f->data) kfree(f->data);
    f->data     = new_buf;
    f->capacity = new_cap;
    return OS_OK;
}

int32_t noxfs_write(vfs_file_t* f, uint32_t offset,
                    const uint8_t* data, uint32_t len) {
    if (!f || !data || len == 0) return 0;
    uint32_t needed = offset + len;
    if (_ensure_capacity(f, needed) != OS_OK) return -1;

    for (uint32_t i = 0; i < len; i++)
        f->data[offset + i] = data[i];
    if (needed > f->size) f->size = needed;

    if (_store_data(f->inode, f->data, f->size) != OS_OK) return -1;
    return (int32_t)len;
}

static os_status_t _dir_add_entry(uint32_t dir_ino, const uint8_t* name,
                                   uint32_t file_ino, uint8_t file_type) {
    noxfs_inode_t dir;
    if (_iread(dir_ino, &dir) != OS_OK) return OS_ERR_IO;

    uint32_t new_sz = dir.size + sizeof(noxfs_dirent_t);
    uint32_t old_sz = dir.size;
    uint8_t* dir_data = _load_data(dir_ino, &old_sz);
    if (!dir_data) return OS_ERR_OOM;

    uint8_t* new_data = (uint8_t*)kmalloc((new_sz + NOXFS_BLKSZ - 1) & ~(NOXFS_BLKSZ - 1));
    if (!new_data) { kfree(dir_data); return OS_ERR_OOM; }
    for (uint32_t i = 0; i < dir.size; i++) new_data[i] = dir_data[i];
    kfree(dir_data);

    noxfs_dirent_t* ent = (noxfs_dirent_t*)(new_data + dir.size);
    ent->inode    = file_ino;
    ent->rec_len  = sizeof(noxfs_dirent_t);
    ent->file_type = file_type;
    ent->name_len  = 0;
    while (name[ent->name_len] && ent->name_len < 23) ent->name_len++;
    for (uint32_t j = 0; j < ent->name_len; j++) ent->name[j] = (char)name[j];
    ent->name[ent->name_len] = 0;

    os_status_t s = _store_data(dir_ino, new_data, new_sz);
    kfree(new_data);
    return s;
}

vfs_file_t* noxfs_creat(const uint8_t* name) {
    if (!g_ready || !name || g_count >= NOXFS_MAX_FILES) return (vfs_file_t*)0;
    if (noxfs_lookup(name)) return (vfs_file_t*)0;

    uint32_t ino = _ialloc(NOXFS_INO_FILE);
    if (ino == (uint32_t)-1) return (vfs_file_t*)0;

    if (_dir_add_entry(g_sb.root_ino, name, ino, NOXFS_FT_FILE) != OS_OK) {
        return (vfs_file_t*)0;
    }

    vfs_file_t* f = &g_files[g_count];
    for (uint32_t j = 0; j < 31 && name[j]; j++) f->name[j] = name[j];
    f->name[31] = 0;
    f->data     = (uint8_t*)kmalloc(NOXFS_BLKSZ);
    if (!f->data) return (vfs_file_t*)0;
    for (uint32_t i = 0; i < NOXFS_BLKSZ; i++) f->data[i] = 0;
    f->size     = 0;
    f->inode    = ino;
    f->capacity = NOXFS_BLKSZ;
    g_count++;

    return f;
}

vfs_file_t* noxfs_creat_at(uint32_t parent_ino, const uint8_t* name) {
    if (!g_ready || !name || g_count >= NOXFS_MAX_FILES) return (vfs_file_t*)0;

    uint32_t ino = _ialloc(NOXFS_INO_FILE);
    if (ino == (uint32_t)-1) return (vfs_file_t*)0;

    if (_dir_add_entry(parent_ino, name, ino, NOXFS_FT_FILE) != OS_OK)
        return (vfs_file_t*)0;

    vfs_file_t* f = &g_files[g_count];
    for (uint32_t j = 0; j < 31 && name[j]; j++) f->name[j] = name[j];
    f->name[31] = 0;
    f->data     = (uint8_t*)kmalloc(NOXFS_BLKSZ);
    if (!f->data) return (vfs_file_t*)0;
    for (uint32_t i = 0; i < NOXFS_BLKSZ; i++) f->data[i] = 0;
    f->size     = 0;
    f->inode    = ino;
    f->capacity = NOXFS_BLKSZ;
    g_count++;
    return f;
}

static uint32_t _dir_lookup_name(uint32_t dir_ino, const uint8_t* name);

/* Create a file at an absolute path, making parent directories as needed. */
vfs_file_t* noxfs_creat_path(const uint8_t* path) {
    if (!g_ready || !path || path[0] != '/') return (vfs_file_t*)0;

    /* Walk each component, creating dirs as needed, stop before the last. */
    uint32_t cur = g_sb.root_ino;
    const uint8_t* p = path + 1;  /* skip leading '/' */

    while (*p) {
        /* Extract next component into seg[]. */
        const uint8_t* start = p;
        while (*p && *p != '/') p++;
        uint32_t seglen = (uint32_t)(p - start);
        if (*p == '/') p++;  /* skip separator */

        uint8_t seg[32];
        if (seglen == 0 || seglen > 31) return (vfs_file_t*)0;
        for (uint32_t i = 0; i < seglen; i++) seg[i] = start[i];
        seg[seglen] = 0;

        if (*p == 0) {
            /* Last component: this is the file to create. */
            vfs_file_t* existing = noxfs_lookup(path);
            if (existing) return existing;
            return noxfs_creat_at(cur, seg);
        }

        /* Intermediate component: resolve or mkdir. */
        uint32_t child = _dir_lookup_name(cur, seg);
        if (child == (uint32_t)-1)
            child = noxfs_mkdir(cur, seg);
        if (child == (uint32_t)-1) return (vfs_file_t*)0;
        cur = child;
    }
    return (vfs_file_t*)0;
}

void noxfs_sync(void) {
    if (!g_ready) return;
    _sb_write();
}

/* ── Phase 2: directory operations ───────────────────────── */

static uint32_t _dir_lookup_name(uint32_t dir_ino, const uint8_t* name) {
    noxfs_inode_t dir;
    if (_iread(dir_ino, &dir) != OS_OK) return (uint32_t)-1;
    if (!(dir.mode & NOXFS_INO_DIR)) return (uint32_t)-1;

    uint32_t dir_sz = dir.size;
    uint8_t* dir_data = _load_data(dir_ino, &dir_sz);
    if (!dir_data) return (uint32_t)-1;

    noxfs_dirent_t* entries = (noxfs_dirent_t*)dir_data;
    uint32_t count = dir_sz / sizeof(noxfs_dirent_t);
    uint32_t result = (uint32_t)-1;

    for (uint32_t i = 0; i < count; i++) {
        if (entries[i].inode == 0) continue;
        uint32_t j = 0;
        while (j < entries[i].name_len && name[j] &&
               entries[i].name[j] == (char)name[j]) j++;
        if (j == entries[i].name_len && name[j] == 0) {
            result = entries[i].inode;
            break;
        }
    }

    kfree(dir_data);
    return result;
}

uint32_t noxfs_resolve(uint32_t base_ino, const uint8_t* path) {
    if (!g_ready || !path) return (uint32_t)-1;
    if (path[0] == 0) return base_ino;

    /* Absolute path: start from root */
    uint32_t cur_ino = base_ino;
    const uint8_t* p = path;

    if (p[0] == '/') {
        cur_ino = g_sb.root_ino;
        p++;
        if (p[0] == 0) return cur_ino;
    }

    while (*p) {
        while (*p == '/') p++;
        if (*p == 0) break;

        /* Extract component name */
        uint8_t comp[32];
        uint32_t ci = 0;
        while (*p && *p != '/' && ci < 31) comp[ci++] = *p++;
        comp[ci] = 0;

        if (ci == 0) continue;

        if (comp[0] == '.' && comp[1] == 0) continue;
        if (comp[0] == '.' && comp[1] == '.' && comp[2] == 0) {
            cur_ino = g_sb.root_ino; /* simplified: parent = root */
            continue;
        }

        cur_ino = _dir_lookup_name(cur_ino, comp);
        if (cur_ino == (uint32_t)-1) return (uint32_t)-1;
    }

    return cur_ino;
}

uint32_t noxfs_mkdir(uint32_t parent_ino, const uint8_t* name) {
    if (!g_ready || !name || name[0] == 0) return (uint32_t)-1;

    if (_dir_lookup_name(parent_ino, name) != (uint32_t)-1)
        return (uint32_t)-1; /* already exists */

    uint32_t dir_ino = _ialloc(NOXFS_INO_DIR);
    if (dir_ino == (uint32_t)-1) return (uint32_t)-1;

    /* Allocate data block for "." and ".." entries */
    uint32_t data_blk = _balloc();
    if (data_blk == (uint32_t)-1) return (uint32_t)-1;

    /* Write "." and ".." directory entries */
    uint8_t dot_data[NOXFS_BLKSZ];
    for (uint32_t i = 0; i < NOXFS_BLKSZ; i++) dot_data[i] = 0;

    noxfs_dirent_t* dot = (noxfs_dirent_t*)dot_data;
    dot->inode     = dir_ino;
    dot->rec_len   = sizeof(noxfs_dirent_t);
    dot->name_len  = 1;
    dot->file_type = NOXFS_FT_DIR;
    dot->name[0]   = '.';

    noxfs_dirent_t* dotdot = (noxfs_dirent_t*)(dot_data + sizeof(noxfs_dirent_t));
    dotdot->inode     = parent_ino;
    dotdot->rec_len   = sizeof(noxfs_dirent_t);
    dotdot->name_len  = 2;
    dotdot->file_type = NOXFS_FT_DIR;
    dotdot->name[0]   = '.';
    dotdot->name[1]   = '.';

    /* Write dot data to the allocated block */
    buf_t* bp = bread(BUF_DEV_ATA, data_blk);
    if (!bp) return (uint32_t)-1;
    for (uint32_t i = 0; i < NOXFS_BLKSZ; i++) bp->data[i] = dot_data[i];
    bp->flags |= B_DIRTY;
    bwrite(bp);
    brelse(bp);

    /* Update new dir inode: blocks[0] = data_blk, size = 2 * 32 = 64 */
    noxfs_inode_t dir_inode;
    if (_iread(dir_ino, &dir_inode) != OS_OK) return (uint32_t)-1;
    dir_inode.blocks[0] = data_blk;
    dir_inode.size      = 2 * sizeof(noxfs_dirent_t);
    if (_iwrite(dir_ino, &dir_inode) != OS_OK) return (uint32_t)-1;

    /* Add entry in parent directory */
    if (_dir_add_entry(parent_ino, name, dir_ino, NOXFS_FT_DIR) != OS_OK)
        return (uint32_t)-1;

    return dir_ino;
}

int32_t noxfs_getdents(uint32_t dir_ino, uint8_t* buf,
                        uint32_t len, uint32_t* off) {
    if (!g_ready || !buf || !off) return -1;
    if (len < sizeof(noxfs_dirent_t)) return -1;

    noxfs_inode_t dir;
    if (_iread(dir_ino, &dir) != OS_OK) return -1;
    if (!(dir.mode & NOXFS_INO_DIR)) return -1;

    uint32_t dir_sz = dir.size;
    uint8_t* dir_data = _load_data(dir_ino, &dir_sz);
    if (!dir_data) return -1;

    uint32_t entry_count = dir_sz / sizeof(noxfs_dirent_t);
    uint32_t entry_idx   = *off / sizeof(noxfs_dirent_t);

    if (entry_idx >= entry_count) { kfree(dir_data); return 0; }

    /* Pack as many dirents as fit into buf — a single call returns the
       whole directory (or as much as the buffer holds), like Linux
       getdents.  This lets callers read the directory in one shot. */
    noxfs_dirent_t* entries = (noxfs_dirent_t*)dir_data;
    uint32_t        written = 0;

    while (entry_idx < entry_count &&
           written + sizeof(noxfs_dirent_t) <= len) {
        uint8_t* src = (uint8_t*)&entries[entry_idx];
        for (uint32_t i = 0; i < sizeof(noxfs_dirent_t); i++)
            buf[written + i] = src[i];
        written += sizeof(noxfs_dirent_t);
        entry_idx++;
    }

    *off = entry_idx * sizeof(noxfs_dirent_t);
    kfree(dir_data);
    return (int32_t)written;
}

os_status_t noxfs_stat(uint32_t ino, vfs_file_t* out) {
    if (!g_ready || !out) return OS_ERR_INVALID;

    noxfs_inode_t in;
    if (_iread(ino, &in) != OS_OK) return OS_ERR_IO;

    out->name[0] = 0;
    out->data    = (uint8_t*)0;
    out->size    = in.size;
    out->inode   = ino;
    out->capacity = ((in.mode & NOXFS_INO_DIR) ? NOXFS_INO_DIR : 0)
                  | ((in.mode & NOXFS_INO_FILE) ? NOXFS_INO_FILE : 0);

    return OS_OK;
}

uint32_t noxfs_root_ino(void) {
    return g_ready ? g_sb.root_ino : (uint32_t)-1;
}
