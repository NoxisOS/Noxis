/**
 * @file    fs/devfs/devfs.c
 * @brief   /dev virtual filesystem.
 */
#include <fs/devfs/devfs.h>
#include <fs/noxfs/noxfs.h>
#include <common/types.h>

/* ── Synthetic file pool (same pattern as procfs) ────────────────────── */
#define POOL  4
static vfs_file_t g_pool[POOL];
static uint8_t    g_dat[POOL][4];  /* device files need no data buffer */
static uint32_t   g_next;

static vfs_file_t* _alloc(uint32_t ino) {
    uint32_t i = g_next; g_next = (g_next + 1) % POOL;
    g_pool[i].data     = g_dat[i];
    g_pool[i].size     = 0;
    g_pool[i].inode    = ino;
    g_pool[i].capacity = 0;
    g_pool[i].name[0]  = 0;
    return &g_pool[i];
}

/* ── Path helpers ────────────────────────────────────────────────────── */

int devfs_is_dev_path(const uint8_t* p) {
    return p[0]=='/' && p[1]=='d' && p[2]=='e' && p[3]=='v'
           && (p[4]==0 || p[4]=='/');
}

vfs_file_t* devfs_lookup(const uint8_t* abs_path) {
    const uint8_t* r = abs_path + 4;   /* skip "/dev" */
    if (r[0] == 0 || (r[0]=='/' && r[1]==0)) return (vfs_file_t*)0; /* dir */
    if (r[0] != '/') return (vfs_file_t*)0;
    r++;

    if (r[0]=='n'&&r[1]=='u'&&r[2]=='l'&&r[3]=='l'&&r[4]==0)
        return _alloc(DEV_NULL_INO);
    if (r[0]=='z'&&r[1]=='e'&&r[2]=='r'&&r[3]=='o'&&r[4]==0)
        return _alloc(DEV_ZERO_INO);
    if (r[0]=='t'&&r[1]=='t'&&r[2]=='y'&&r[3]==0)
        return _alloc(DEV_TTY_INO);
    if (r[0]=='r'&&r[1]=='a'&&r[2]=='n'&&r[3]=='d'&&r[4]=='o'&&r[5]=='m'&&r[6]==0)
        return _alloc(DEV_RANDOM_INO);

    return (vfs_file_t*)0;
}

uint32_t devfs_resolve(const uint8_t* abs_path) {
    const uint8_t* r = abs_path + 4;
    if (r[0]==0 || (r[0]=='/'&&r[1]==0)) return DEV_ROOT_INO;
    vfs_file_t* f = devfs_lookup(abs_path);
    return f ? f->inode : (uint32_t)-1;
}

/* ── getdents ────────────────────────────────────────────────────────── */

typedef struct __attribute__((packed)) {
    uint32_t inode; uint16_t rec_len; uint8_t name_len; uint8_t file_type;
    char name[24];
} _dde_t;

static uint32_t _ent(uint8_t* buf, uint32_t pos, uint32_t len,
                     uint32_t ino, uint8_t type, const char* name) {
    if (pos + sizeof(_dde_t) > len) return pos;
    _dde_t* d = (_dde_t*)(buf + pos);
    d->inode = ino; d->rec_len = sizeof(_dde_t); d->file_type = type;
    uint8_t nl = 0;
    while (name[nl] && nl < 23) { d->name[nl] = name[nl]; nl++; }
    d->name[nl] = 0; d->name_len = nl;
    return pos + sizeof(_dde_t);
}

int32_t devfs_getdents(uint32_t dir_ino, uint8_t* buf,
                        uint32_t len, uint32_t* off) {
    if (!buf || !off || len < sizeof(_dde_t)) return -1;
    (void)dir_ino;  /* only /dev root supported */

    static const struct { const char* name; uint32_t ino; uint8_t type; } _devs[] = {
        { ".",      DEV_ROOT_INO,   2 },
        { "..",     DEV_ROOT_INO,   2 },
        { "null",   DEV_NULL_INO,   1 },
        { "zero",   DEV_ZERO_INO,   1 },
        { "tty",    DEV_TTY_INO,    1 },
        { "random", DEV_RANDOM_INO, 1 },
        { (const char*)0, 0, 0 }
    };

    uint32_t esz  = sizeof(_dde_t);
    uint32_t idx  = *off / esz;
    uint32_t pos  = 0;

    for (uint32_t i = idx; _devs[i].name && pos + esz <= len; i++) {
        pos = _ent(buf, pos, len, _devs[i].ino, _devs[i].type, _devs[i].name);
        (*off) += esz;
    }
    return pos > 0 ? (int32_t)pos : 0;
}
