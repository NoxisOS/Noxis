/**
 * @file    fs/procfs/procfs.h
 * @brief   Synthetic /proc filesystem.
 *
 * Pseudo-inode scheme (never clashes with NoxFS inodes which start at 0):
 *   PROCFS_ROOT_INO       — /proc  (directory)
 *   PROCFS_PID_INO(pid)   — /proc/<pid>  (directory)
 *
 * Files under /proc have no persistent inode; they are looked up by path
 * via procfs_lookup() which returns a pointer to a small static pool of
 * synthetic vfs_file_t structs with dynamically-generated content.
 */
#ifndef FS_PROCFS_H
#define FS_PROCFS_H

#include <common/types.h>
#include <fs/vfs/vfs.h>

#define PROCFS_ROOT_INO      0xFFFFFFFEu
#define PROCFS_PID_INO(pid)  (0xFFFF0000u | ((uint32_t)(pid) & 0xFFFFu))
#define IS_PROCFS_INO(ino)   ((ino) >= 0xFFFF0000u)

/* Return 1 if `path` is an absolute /proc path. */
int         procfs_is_proc_path(const uint8_t* path);

/* Look up a /proc file by absolute path.
 * Returns a pointer to a synthetic (pooled) vfs_file_t, or NULL if the
 * path is a directory or does not exist.  The returned pointer is valid
 * until the next call that recycles the same pool slot (~8 calls). */
vfs_file_t* procfs_lookup(const uint8_t* abs_path);

/* Resolve a /proc path to its pseudo-inode (for directory opens). */
uint32_t    procfs_resolve(const uint8_t* abs_path);

/* Fill `buf` with noxfs_dirent_t-compatible entries for a procfs directory.
 * `dir_ino` must be a procfs pseudo-inode.  `off` is updated in place.
 * Returns bytes written, 0 at end, -1 on error. */
int32_t     procfs_getdents(uint32_t dir_ino, uint8_t* buf,
                             uint32_t len, uint32_t* off);

/* Return 1 if `ino` is a procfs directory inode. */
int         procfs_is_dir(uint32_t ino);

#endif /* FS_PROCFS_H */
