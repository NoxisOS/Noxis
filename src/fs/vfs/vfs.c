/**
 * @file    fs/vfs.c
 * @brief   VFS facade — try NoxFS on the ATA disk first; fall back to
 *          the in-kernel ramfs if the disk is missing or unformatted.
 * @author  Noxis Team
 * @date    2026-05-30
 */
#include <fs/vfs/vfs.h>

extern os_status_t  ramfs_init(void);
extern uint32_t     ramfs_count(void);
extern vfs_file_t*  ramfs_entry(uint32_t i);
extern vfs_file_t*  ramfs_lookup(const uint8_t* name);

extern os_status_t  noxfs_init(void);
extern uint32_t     noxfs_count(void);
extern vfs_file_t*  noxfs_entry(uint32_t i);
extern vfs_file_t*  noxfs_lookup(const uint8_t* name);
extern int32_t      noxfs_write(vfs_file_t* f, uint32_t offset,
                                const uint8_t* data, uint32_t len);
extern vfs_file_t*  noxfs_creat(const uint8_t* name);
extern void         noxfs_sync(void);
extern uint32_t     noxfs_root_ino(void);
extern uint32_t     noxfs_resolve(uint32_t base_ino, const uint8_t* path);
extern int32_t      noxfs_getdents(uint32_t dir_ino, uint8_t* buf,
                                   uint32_t len, uint32_t* off);
extern os_status_t  noxfs_stat(uint32_t ino, vfs_file_t* out);

static int _use_noxfs;

os_status_t vfs_init(void) {
    if (noxfs_init() == OS_OK) {
        _use_noxfs = 1;
        return OS_OK;
    }
    _use_noxfs = 0;
    return ramfs_init();
}

uint32_t vfs_count(void) {
    return _use_noxfs ? noxfs_count() : ramfs_count();
}

vfs_file_t* vfs_entry(uint32_t i) {
    return _use_noxfs ? noxfs_entry(i) : ramfs_entry(i);
}

vfs_file_t* vfs_lookup(const uint8_t* name) {
    return _use_noxfs ? noxfs_lookup(name) : ramfs_lookup(name);
}

int32_t vfs_write_file(vfs_file_t* f, uint32_t offset,
                       const uint8_t* data, uint32_t len) {
    if (!f || !data || len == 0) return 0;
    if (!_use_noxfs) return -1;
    return noxfs_write(f, offset, data, len);
}

vfs_file_t* vfs_creat(const uint8_t* name) {
    if (!_use_noxfs) return (vfs_file_t*)0;
    return noxfs_creat(name);
}

void vfs_sync(void) {
    if (_use_noxfs) noxfs_sync();
}

uint32_t vfs_root_ino(void) {
    return _use_noxfs ? noxfs_root_ino() : (uint32_t)0;
}

uint32_t vfs_resolve_ino(uint32_t cwd_ino, const uint8_t* path) {
    if (!_use_noxfs) return (uint32_t)-1;
    return noxfs_resolve(cwd_ino, path);
}

vfs_file_t* vfs_lookup_at(uint32_t cwd_ino, const uint8_t* path) {
    if (!_use_noxfs) return vfs_lookup(path);
    /* Absolute path: delegate to plain lookup for cache hit */
    if (path[0] == '/') return noxfs_lookup(path);
    /* Relative: resolve inode first, then build a vfs_file_t via lookup */
    uint32_t ino = noxfs_resolve(cwd_ino, path);
    if (ino == (uint32_t)-1) return (vfs_file_t*)0;
    /* Walk noxfs file table to find by inode */
    for (uint32_t i = 0; i < noxfs_count(); i++) {
        vfs_file_t* f = noxfs_entry(i);
        if (f && f->inode == ino) return f;
    }
    /* Not in cache yet — resolve as a path from root for the name */
    const uint8_t* base = path;
    for (const uint8_t* p = path; *p; p++)
        if (*p == '/') base = p + 1;
    return noxfs_lookup(base);   /* fallback: look up by basename */
}

int32_t vfs_getdents(uint32_t dir_ino, uint8_t* buf,
                     uint32_t len, uint32_t* off) {
    if (!_use_noxfs) return -1;
    return noxfs_getdents(dir_ino, buf, len, off);
}

int vfs_is_dir(uint32_t ino) {
    if (!_use_noxfs) return 0;
    vfs_file_t st;
    if (noxfs_stat(ino, &st) != 0) return 0;
    return (st.capacity & 0x4000u) ? 1 : 0;  /* NOXFS_INO_DIR = 0x4000 */
}
