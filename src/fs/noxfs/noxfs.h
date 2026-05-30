/**
 * @file    fs/noxfs/noxfs.h
 * @brief   NoxFS v2 — inode-based filesystem with block/inode bitmaps.
 *          Phase 1: flat root directory with inode-backed files.
 * @author  Noxis Team
 * @date    2026-05-30
 */
#ifndef FS_NOXFS_NOXFS_H
#define FS_NOXFS_NOXFS_H

#include <common/types.h>
#include <common/status.h>
#include <fs/vfs/vfs.h>

/* ── on-disk layout ─────────────────────────────────────────── */
#define NOXFS_MAGIC     0x4E584632u   /* "NXF2" */
#define NOXFS_BLKSZ     512
#define NOXFS_INO_SZ    64
#define NOXFS_DIRECT    10            /* direct blocks per inode */
#define NOXFS_INDIRECT  (NOXFS_BLKSZ / 4)  /* pointers in indirect block */

#define NOXFS_MAX_FILES 24

/* ── superblock (block 0) ────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t block_count;
    uint32_t inode_count;
    uint32_t free_blocks;
    uint32_t free_inodes;
    uint32_t blk_bmp;     /* LBA of block bitmap */
    uint32_t ino_bmp;     /* LBA of inode bitmap */
    uint32_t ino_tbl;     /* LBA of first inode table block */
    uint32_t data_start;  /* first free data block after metadata */
    uint32_t root_ino;    /* root directory inode number */
    uint8_t  pad[472];
} noxfs_sb_t;

/* ── inode (64 bytes) ────────────────────────────────────────── */
#define NOXFS_INO_FILE  0x8000
#define NOXFS_INO_DIR   0x4000

typedef struct __attribute__((packed)) {
    uint16_t mode;
    uint16_t uid;
    uint16_t gid;
    uint32_t size;
    uint32_t blocks[NOXFS_DIRECT];
    uint32_t indirect;
    uint16_t links;
    uint8_t  pad[8];       /* 2+2+2+4+40+4+2+8 = 64 */
} noxfs_inode_t;

/* ── directory entry (32 bytes) ──────────────────────────────── */
#define NOXFS_FT_UNKNOWN  0
#define NOXFS_FT_FILE     1
#define NOXFS_FT_DIR      2

typedef struct __attribute__((packed)) {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[24];
} noxfs_dirent_t;

/* ── public API ──────────────────────────────────────────────── */
os_status_t   noxfs_init(void);
uint32_t      noxfs_count(void);
vfs_file_t*   noxfs_entry(uint32_t i);
vfs_file_t*   noxfs_lookup(const uint8_t* name);
int32_t       noxfs_write(vfs_file_t* f, uint32_t off,
                          const uint8_t* data, uint32_t len);
vfs_file_t*   noxfs_creat(const uint8_t* name);
void          noxfs_sync(void);

#endif
