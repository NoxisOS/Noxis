/**
 * @file    fs/devfs/devfs.h
 * @brief   Synthetic /dev filesystem.
 *
 * Devices exposed:
 *   /dev/null    — writes discarded, reads return EOF (0 bytes)
 *   /dev/zero    — reads return 0x00 bytes, writes discarded
 *   /dev/tty     — reads/writes routed to the system console
 *   /dev/random  — reads return LCG pseudo-random bytes
 *
 * Magic inode numbers (in vfs_file_t.inode) identify device files so
 * sys_open can set the correct fd kind without touching disk.
 */
#ifndef FS_DEVFS_H
#define FS_DEVFS_H

#include <common/types.h>
#include <fs/vfs/vfs.h>

/* Pseudo-inode values — must not collide with NoxFS or procfs ranges. */
#define DEV_ROOT_INO    0xFFFE0000u
#define DEV_NULL_INO    0xFFFE0001u
#define DEV_ZERO_INO    0xFFFE0002u
#define DEV_TTY_INO     0xFFFE0003u
#define DEV_RANDOM_INO  0xFFFE0004u

#define IS_DEVFS_INO(i) ((i) >= DEV_ROOT_INO)

/* Return 1 if `path` is an absolute /dev path. */
int         devfs_is_dev_path(const uint8_t* path);

/* Return synthetic vfs_file_t for a /dev file (NULL for directories). */
vfs_file_t* devfs_lookup(const uint8_t* abs_path);

/* Resolve a /dev path to its pseudo-inode. */
uint32_t    devfs_resolve(const uint8_t* abs_path);

/* Fill dirent entries for a /dev directory. */
int32_t     devfs_getdents(uint32_t dir_ino, uint8_t* buf,
                            uint32_t len, uint32_t* off);

#endif /* FS_DEVFS_H */
