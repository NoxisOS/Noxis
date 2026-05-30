/**
 * @file    fs/vfs.c
 * @brief   VFS facade — try NoxFS on the ATA disk first; fall back to
 *          the in-kernel ramfs if the disk is missing or unformatted.
 * @author  Noxis Team
 * @date    2026-05-30
 */
#include <fs/vfs.h>

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
