/**
 * @file    fs/noxfs.h
 * @brief   NoxFS — minimal disk-backed filesystem (read/write, single dir).
 * @author  Noxis Team
 * @date    2026-05-30
 */
#ifndef FS_NOXFS_H
#define FS_NOXFS_H

#include <common/types.h>
#include <common/status.h>
#include <fs/vfs.h>

os_status_t        noxfs_init(void);
uint32_t           noxfs_count(void);
vfs_file_t*        noxfs_entry(uint32_t i);
vfs_file_t*        noxfs_lookup(const uint8_t* name);

/**
 * @brief Write data to a file at the given offset.  Grows the file if
 *        offset+len exceeds current size.  Returns bytes written.
 */
int32_t            noxfs_write(vfs_file_t* f, uint32_t offset,
                               const uint8_t* data, uint32_t len);

/**
 * @brief Create a new empty file.  Allocates one sector and writes the
 *        updated TOC to disk.  Returns the new file pointer, or NULL.
 */
vfs_file_t*        noxfs_creat(const uint8_t* name);

/**
 * @brief Flush all dirty buffers for this filesystem to disk.
 */
void               noxfs_sync(void);

#endif
