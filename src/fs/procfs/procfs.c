/**
 * @file    fs/procfs/procfs.c
 * @brief   Synthetic /proc filesystem implementation.
 *
 * Exposed files:
 *   /proc/uptime          — kernel uptime in milliseconds
 *   /proc/meminfo         — free / total physical memory
 *   /proc/<pid>/status    — process name, pid, and state
 */
#include <fs/procfs/procfs.h>
#include <fs/noxfs/noxfs.h>      /* dirent layout for getdents */
#include <proc/scheduler.h>
#include <proc/process.h>
#include <drivers/pit.h>
#include <mm/phys/pmm.h>
#include <common/types.h>

/* ── Synthetic file pool ─────────────────────────────────────────────── */

#define POOL_SLOTS  8
#define POOL_BUFSZ  512

static vfs_file_t g_pool[POOL_SLOTS];
static uint8_t    g_data[POOL_SLOTS][POOL_BUFSZ];
static uint32_t   g_next;

static vfs_file_t* _pool_alloc(void) {
    uint32_t i = g_next;
    g_next = (g_next + 1) % POOL_SLOTS;
    g_pool[i].data     = g_data[i];
    g_pool[i].size     = 0;
    g_pool[i].inode    = 0;
    g_pool[i].capacity = POOL_BUFSZ;
    g_pool[i].name[0]  = 0;
    return &g_pool[i];
}

/* ── Buffer helpers ──────────────────────────────────────────────────── */

static uint32_t _puts(uint8_t* b, uint32_t pos, const char* s) {
    while (*s && pos < POOL_BUFSZ - 1) b[pos++] = (uint8_t)*s++;
    return pos;
}

static uint32_t _putu(uint8_t* b, uint32_t pos, uint64_t v) {
    char tmp[24]; int i = 24; tmp[--i] = 0;
    if (!v) { tmp[--i] = '0'; }
    else    { while (v) { tmp[--i] = (char)('0' + v % 10); v /= 10; } }
    return _puts(b, pos, &tmp[i]);
}

/* ── Path helpers ────────────────────────────────────────────────────── */

int procfs_is_proc_path(const uint8_t* p) {
    return p[0]=='/' && p[1]=='p' && p[2]=='r' && p[3]=='o' && p[4]=='c'
           && (p[5]==0 || p[5]=='/');
}

/* Skip "/proc" prefix; return pointer to the rest (may be "" or "/..."). */
static const uint8_t* _after_proc(const uint8_t* path) {
    return path + 5;   /* skip "/proc" */
}

/* Parse a decimal number from a string; returns 0 if none. */
static uint64_t _parse_uint(const uint8_t* s) {
    uint64_t v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s++ - '0'); }
    return v;
}

/* ── Content generators ──────────────────────────────────────────────── */

static const char* _state_name(proc_state_t s) {
    switch (s) {
    case PROC_READY:   return "ready";
    case PROC_RUNNING: return "running";
    case PROC_BLOCKED: return "blocked";
    case PROC_ZOMBIE:  return "zombie";
    default:           return "unknown";
    }
}

static vfs_file_t* _gen_uptime(void) {
    vfs_file_t* f = _pool_alloc();
    uint32_t p = 0;
    p = _putu(f->data, p, pit_uptime_ms());
    p = _puts(f->data, p, " ms\n");
    f->size = p;
    return f;
}

static vfs_file_t* _gen_meminfo(void) {
    vfs_file_t* f = _pool_alloc();
    uint32_t p = 0;
    uint64_t free_kb  = pmm_free_count() * 4;   /* 4 KB per frame */
    uint64_t total_kb = 128 * 1024;              /* 128 MB total (from pmm_init) */
    p = _puts(f->data, p, "MemFree:  "); p = _putu(f->data, p, free_kb);  p = _puts(f->data, p, " kB\n");
    p = _puts(f->data, p, "MemTotal: "); p = _putu(f->data, p, total_kb); p = _puts(f->data, p, " kB\n");
    f->size = p;
    return f;
}

static vfs_file_t* _gen_status(process_t* proc) {
    vfs_file_t* f = _pool_alloc();
    uint32_t p = 0;
    p = _puts(f->data, p, "Name:  "); p = _puts(f->data, p, (const char*)proc->name); p = _puts(f->data, p, "\n");
    p = _puts(f->data, p, "Pid:   "); p = _putu(f->data, p, proc->pid);               p = _puts(f->data, p, "\n");
    p = _puts(f->data, p, "State: "); p = _puts(f->data, p, _state_name(proc->state)); p = _puts(f->data, p, "\n");
    p = _puts(f->data, p, "Brk:   0x");
    /* write brk in hex */
    uint64_t v = proc->brk; char hex[17]; int hi = 16; hex[hi] = 0;
    if (!v) { hex[--hi] = '0'; }
    else { while (v) { int d = (int)(v & 0xF); hex[--hi] = (char)(d<10?'0'+d:'a'+d-10); v >>= 4; } }
    p = _puts(f->data, p, &hex[hi]); p = _puts(f->data, p, "\n");
    f->size = p;
    return f;
}

/* ── Public API ──────────────────────────────────────────────────────── */

vfs_file_t* procfs_lookup(const uint8_t* abs_path) {
    const uint8_t* rest = _after_proc(abs_path);

    /* /proc  or  /proc/  → directory, no file */
    if (rest[0] == 0 || (rest[0] == '/' && rest[1] == 0)) return (vfs_file_t*)0;

    if (rest[0] != '/') return (vfs_file_t*)0;
    rest++;   /* skip '/' */

    /* /proc/uptime */
    if (rest[0]=='u' && rest[1]=='p' && rest[2]=='t' && rest[3]=='i' &&
        rest[4]=='m' && rest[5]=='e' && rest[6]==0)
        return _gen_uptime();

    /* /proc/meminfo */
    if (rest[0]=='m' && rest[1]=='e' && rest[2]=='m' && rest[3]=='i' &&
        rest[4]=='n' && rest[5]=='f' && rest[6]=='o' && rest[7]==0)
        return _gen_meminfo();

    /* /proc/<pid>  or  /proc/<pid>/status */
    if (rest[0] >= '0' && rest[0] <= '9') {
        uint64_t pid = _parse_uint(rest);
        while (*rest >= '0' && *rest <= '9') rest++;

        if (rest[0] == 0) return (vfs_file_t*)0;  /* /proc/<pid> is a dir */

        if (rest[0] == '/' &&
            rest[1]=='s' && rest[2]=='t' && rest[3]=='a' && rest[4]=='t' &&
            rest[5]=='u' && rest[6]=='s' && rest[7]==0) {
            process_t* p = scheduler_find(pid);
            return p ? _gen_status(p) : (vfs_file_t*)0;
        }
    }
    return (vfs_file_t*)0;
}

uint32_t procfs_resolve(const uint8_t* abs_path) {
    const uint8_t* rest = _after_proc(abs_path);
    if (rest[0] == 0 || (rest[0] == '/' && rest[1] == 0))
        return PROCFS_ROOT_INO;
    if (rest[0] != '/') return (uint32_t)-1;
    rest++;
    if (rest[0] >= '0' && rest[0] <= '9') {
        uint64_t pid = _parse_uint(rest);
        while (*rest >= '0' && *rest <= '9') rest++;
        if (rest[0] == 0) return PROCFS_PID_INO(pid);
    }
    /* File paths (/proc/uptime etc.) don't have a persistent inode */
    return (uint32_t)-1;
}

int procfs_is_dir(uint32_t ino) {
    return IS_PROCFS_INO(ino);
}

/* ── getdents ────────────────────────────────────────────────────────── */

/* Dirent entry layout must match noxfs_dirent_t (32 bytes). */
typedef struct __attribute__((packed)) {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;   /* 1=file, 2=dir */
    char     name[24];
} _pde_t;

static uint32_t _write_dirent(uint8_t* buf, uint32_t pos, uint32_t len,
                               uint32_t ino, uint8_t type, const char* name) {
    if (pos + sizeof(_pde_t) > len) return pos;
    _pde_t* d = (_pde_t*)(buf + pos);
    d->inode     = ino;
    d->rec_len   = sizeof(_pde_t);
    d->file_type = type;
    uint8_t nl = 0;
    while (name[nl] && nl < 23) { d->name[nl] = name[nl]; nl++; }
    d->name[nl]  = 0;
    d->name_len  = nl;
    return pos + sizeof(_pde_t);
}

static uint32_t _uint_to_str(uint64_t v, char* out) {
    char tmp[24]; int i = 24; tmp[--i] = 0;
    if (!v) tmp[--i] = '0';
    else while (v) { tmp[--i] = (char)('0' + v % 10); v /= 10; }
    uint32_t len = 0;
    while (tmp[i]) { out[len++] = tmp[i++]; }
    out[len] = 0;
    return len;
}

int32_t procfs_getdents(uint32_t dir_ino, uint8_t* buf,
                         uint32_t len, uint32_t* off) {
    if (!buf || len < sizeof(_pde_t) || !off) return -1;

    uint32_t pos = 0;
    uint32_t entry_size = sizeof(_pde_t);
    uint32_t entry_idx  = *off / entry_size;
    uint32_t written    = 0;

    if (dir_ino == PROCFS_ROOT_INO) {
        /* Enumerate: . .. uptime meminfo <pid dirs> */
        const char* fixed[] = { ".", "..", "uptime", "meminfo", (const char*)0 };
        uint8_t     ftype[] = { 2, 2, 1, 1 };
        uint32_t    fino[]  = { PROCFS_ROOT_INO, PROCFS_ROOT_INO, 1, 2 };
        uint32_t    fidx = 0;

        /* Fixed entries */
        while (fixed[fidx] && entry_idx > 0) { fidx++; entry_idx--; }
        while (fixed[fidx] && pos + entry_size <= len) {
            pos = _write_dirent(buf, pos, len, fino[fidx], ftype[fidx], fixed[fidx]);
            fidx++; written++;
        }
        if (fixed[fidx]) { *off = (*off / entry_size) * entry_size + written * entry_size; return (int32_t)pos; }

        /* Process directories */
        uint32_t proc_idx = entry_idx;  /* entries skipped within process list */
        for (uint32_t i = 0; ; i++) {
            process_t* p = scheduler_at(i);
            if (!p) break;
            if (proc_idx > 0) { proc_idx--; continue; }
            if (pos + entry_size > len) break;
            char name[24];
            _uint_to_str(p->pid, name);
            pos = _write_dirent(buf, pos, len, PROCFS_PID_INO(p->pid), 2, name);
            written++;
        }
    } else {
        /* /proc/<pid> directory: ., .., status */
        uint64_t pid = dir_ino & 0xFFFFu;
        const char* fixed[] = { ".", "..", "status", (const char*)0 };
        uint8_t     ftype[] = { 2, 2, 1 };
        while (fixed[entry_idx] && pos + entry_size <= len) {
            pos = _write_dirent(buf, pos, len,
                                entry_idx < 2 ? dir_ino : (uint32_t)pid,
                                ftype[entry_idx], fixed[entry_idx]);
            entry_idx++; written++;
        }
        (void)pid;
    }

    *off += written * entry_size;
    return pos > 0 ? (int32_t)pos : 0;
}
