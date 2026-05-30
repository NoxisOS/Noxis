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

#endif /* FS_VFS_H */
