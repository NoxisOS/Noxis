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

/* A read-only file view. `data` is the live backing buffer (don't free). */
typedef struct {
    const uint8_t* name;
    const uint8_t* data;
    uint32_t       size;
} vfs_file_t;

os_status_t        vfs_init(void);
uint32_t           vfs_count(void);
const vfs_file_t*  vfs_entry(uint32_t i);
const vfs_file_t*  vfs_lookup(const uint8_t* name);

#endif /* FS_VFS_H */
