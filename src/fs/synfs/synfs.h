/**
 * @file    fs/synfs/synfs.h
 * @brief   Synthetic filesystem — "everything is a file" for kernel state.
 *
 * synfs is a VFS backend that exposes live kernel state as readable files
 * under /proc and /dev, the same way Linux mounts procfs and devtmpfs.
 * Content is generated on read, never stored:
 *
 *   /proc/meminfo   physical + heap memory summary
 *   /proc/slab      slab cache live/free/peak counts
 *   /proc/sched     process table (pid, state, name)
 *   /proc/uptime    milliseconds since boot
 *   /dev/null       discards writes, reads EOF
 *   /dev/zero       reads infinite zero bytes
 *   /dev/random     reads pseudo-random bytes
 *
 * A node is matched by exact path (static table) or by a dynamic rule
 * (e.g. /proc/<pid>/status).  The syscall layer (sys_open/read/write)
 * dispatches FD_SYN descriptors here.
 */
#ifndef FS_SYNFS_H
#define FS_SYNFS_H

#include <common/types.h>

/* ── String builder used by content generators ──────────────── */
typedef struct {
    uint8_t *buf;
    uint32_t cap;
    uint32_t len;
} sbuf_t;

void sb_char(sbuf_t *sb, char c);
void sb_str (sbuf_t *sb, const char *s);
void sb_u32 (sbuf_t *sb, uint32_t v);
void sb_pad (sbuf_t *sb, uint32_t to_col);   /* pad with spaces to column */

/* ── Node kinds ──────────────────────────────────────────────── */
typedef enum {
    SYN_GEN = 0,   /* text generator (proc files)        */
    SYN_NULL,      /* read → EOF, write → discard         */
    SYN_ZERO,      /* read → zeros, write → discard       */
    SYN_RANDOM,    /* read → pseudo-random bytes          */
} synfs_kind_t;

struct synfs_node;
typedef void (*synfs_gen_fn)(sbuf_t *sb, uint32_t arg);

typedef struct synfs_node {
    const char   *path;    /* e.g. "/proc/meminfo" */
    synfs_kind_t  kind;
    synfs_gen_fn  gen;      /* for SYN_GEN */
    uint32_t      arg;      /* generic (e.g. pid) */
} synfs_node_t;

/* ── API ─────────────────────────────────────────────────────── */

/* Register the built-in /proc and /dev nodes. Call once at boot. */
void          synfs_init(void);

/* 1 if `path` belongs to synfs (starts with /proc or /dev). */
int           synfs_is_synthetic(const char *path);

/* Resolve a path to a node, or NULL.  Handles dynamic /proc/<pid>. */
synfs_node_t *synfs_lookup(const char *path);

/* Read up to `len` bytes at `off` from a node into `buf`.
 * Returns bytes read (0 = EOF), or -1 on error. */
int32_t       synfs_read (synfs_node_t *n, uint32_t off,
                          uint8_t *buf, uint32_t len);

/* Write `len` bytes to a node (mostly discarded). Returns bytes "written". */
int32_t       synfs_write(synfs_node_t *n, uint32_t off,
                          const uint8_t *buf, uint32_t len);

/* Total size of a node's current content (for stat/SEEK_END). */
uint32_t      synfs_size (synfs_node_t *n);

/* Enumerate registered nodes (for directory listing / ls). */
uint32_t      synfs_count(void);
synfs_node_t *synfs_at(uint32_t i);

#endif /* FS_SYNFS_H */
