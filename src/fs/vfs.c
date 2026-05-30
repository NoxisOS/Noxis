/**
 * @file    fs/vfs.c
 * @brief   VFS facade — currently dispatches to a single ramfs backend.
 *          Future: register multiple backends, mount points, fd table.
 * @author  Noxis Team
 * @date    2026-05-30
 */
#include <fs/vfs.h>

extern os_status_t        ramfs_init(void);
extern uint32_t           ramfs_count(void);
extern const vfs_file_t*  ramfs_entry(uint32_t i);
extern const vfs_file_t*  ramfs_lookup(const uint8_t* name);

os_status_t       vfs_init(void)                      { return ramfs_init(); }
uint32_t          vfs_count(void)                     { return ramfs_count(); }
const vfs_file_t* vfs_entry(uint32_t i)               { return ramfs_entry(i); }
const vfs_file_t* vfs_lookup(const uint8_t* name)     { return ramfs_lookup(name); }
