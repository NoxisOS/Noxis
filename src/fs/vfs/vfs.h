/**
 * @file    fs/vfs.h
 * @brief   Virtual filesystem facade — backend-agnostic file access
 * @author  Noxis Team
 * @date    2026-05-30
 */
#ifndef FS_VFS_H
#define FS_VFS_H

#include <common/types.h>
#include <common/status.h>

/* A file view.  `data` is the live backing buffer owned by the backend. */
typedef struct {
    uint8_t        name[32];
    uint8_t*       data;
    uint32_t       size;
    uint32_t       inode;      /* inode number (0 = ramfs) */
    uint32_t       capacity;   /* allocated size (rounded up to sectors) */
} vfs_file_t;

os_status_t   vfs_init(void);
uint32_t      vfs_count(void);
vfs_file_t*   vfs_entry(uint32_t i);
vfs_file_t*   vfs_lookup(const uint8_t* name);

/* Write data to a file at offset.  Grows the file.  Returns bytes written. */
int32_t       vfs_write_file(vfs_file_t* f, uint32_t offset,
                             const uint8_t* data, uint32_t len);

/* Create an empty file.  Returns the new file, or NULL. */
vfs_file_t*   vfs_creat(const uint8_t* name);

/* Flush all dirty metadata to disk. */
void          vfs_sync(void);

/* ── Path / directory helpers ─────────────────────────────────────────── */

/* Root directory inode (0 if VFS not ready). */
uint32_t      vfs_root_ino(void);

/* Resolve `path` relative to `cwd_ino`; return target inode or (uint32_t)-1. */
uint32_t      vfs_resolve_ino(uint32_t cwd_ino, const uint8_t* path);

/* Like vfs_lookup but resolves `path` relative to `cwd_ino`. */
vfs_file_t*   vfs_lookup_at(uint32_t cwd_ino, const uint8_t* path);

/* Read directory entries from `dir_ino` into `buf` (len bytes).
   `off` is updated to the next entry's byte offset.  Returns bytes placed
   in buf, 0 at end of directory, -1 on error. */
int32_t       vfs_getdents(uint32_t dir_ino, uint8_t* buf,
                           uint32_t len, uint32_t* off);

/* Return 1 if `ino` is a directory, 0 otherwise. */
int           vfs_is_dir(uint32_t ino);

#endif /* FS_VFS_H */
