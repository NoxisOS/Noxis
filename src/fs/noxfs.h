/**
 * @file    fs/noxfs.h
 * @brief   NoxFS — minimal disk-backed filesystem (read-only, single dir).
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
const vfs_file_t*  noxfs_entry(uint32_t i);
const vfs_file_t*  noxfs_lookup(const uint8_t* name);

#endif
