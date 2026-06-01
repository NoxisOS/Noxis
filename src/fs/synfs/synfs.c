/**
 * @file    fs/synfs/synfs.c
 * @brief   Synthetic filesystem — live kernel state as files (/proc, /dev).
 */
#include <fs/synfs/synfs.h>
#include <mm/phys/pmm.h>
#include <mm/virt/heap.h>
#include <mm/virt/paging.h>
#include <mm/slab.h>
#include <proc/process.h>
#include <proc/scheduler.h>
#include <drivers/pit.h>
#include <common/types.h>

/* ── String builder ──────────────────────────────────────────── */

void sb_char(sbuf_t *sb, char c) {
    if (sb->len < sb->cap) sb->buf[sb->len] = (uint8_t)c;
    sb->len++;
}

void sb_str(sbuf_t *sb, const char *s) {
    while (*s) sb_char(sb, *s++);
}

void sb_u32(sbuf_t *sb, uint32_t v) {
    char tmp[10];
    int  i = 0;
    if (v == 0) { sb_char(sb, '0'); return; }
    while (v) { tmp[i++] = '0' + (v % 10); v /= 10; }
    while (i--) sb_char(sb, tmp[i]);
}

void sb_pad(sbuf_t *sb, uint32_t to_col) {
    /* Column = bytes since the last newline (not absolute buffer length),
       so padding aligns correctly on every line of multi-line output. */
    uint32_t col = 0;
    uint32_t i = sb->len < sb->cap ? sb->len : sb->cap;
    while (i > 0 && sb->buf[i - 1] != '\n') { col++; i--; }
    while (col < to_col) { sb_char(sb, ' '); col++; }
}

/* ── /proc generators ────────────────────────────────────────── */

static void _gen_meminfo(sbuf_t *sb, uint32_t arg) {
    (void)arg;
    uint32_t total = pmm_get_total_frames();
    uint32_t freef = pmm_get_free_count();
    uint32_t used  = total - freef;

    sb_str(sb, "PhysTotal:  "); sb_u32(sb, total * 4);      sb_str(sb, " KB\n");
    sb_str(sb, "PhysUsed:   "); sb_u32(sb, used  * 4);      sb_str(sb, " KB\n");
    sb_str(sb, "PhysFree:   "); sb_u32(sb, freef * 4);      sb_str(sb, " KB\n");
    sb_str(sb, "HeapFree:   "); sb_u32(sb, heap_get_free() / 1024); sb_str(sb, " KB\n");
    sb_str(sb, "\nHeap by subsystem:\n");
    for (uint32_t t = 0; t < MEM_TAG__COUNT; t++) {
        uint32_t b = heap_tag_bytes((mem_tag_t)t);
        uint32_t n = heap_tag_allocs((mem_tag_t)t);
        if (b == 0 && n == 0) continue;
        sb_str(sb, "  ");
        sb_str(sb, heap_tag_name((mem_tag_t)t));
        sb_pad(sb, 14);
        sb_u32(sb, b); sb_str(sb, " B  (");
        sb_u32(sb, n); sb_str(sb, " allocs)\n");
    }
}

static void _gen_slab_row(sbuf_t *sb, slab_cache_t *c) {
    if (!c) return;
    sb_str(sb, "  ");
    sb_str(sb, c->name);
    sb_pad(sb, 16);
    sb_str(sb, "live="); sb_u32(sb, c->n_alloc);
    sb_str(sb, " free="); sb_u32(sb, c->n_free);
    sb_str(sb, " peak="); sb_u32(sb, c->n_alloc_peak);
    sb_str(sb, " obj="); sb_u32(sb, c->obj_size); sb_str(sb, "B\n");
}

static void _gen_slab(sbuf_t *sb, uint32_t arg) {
    (void)arg;
    sb_str(sb, "slab caches:\n");
    _gen_slab_row(sb, g_process_slab);
    _gen_slab_row(sb, g_pipe_slab);
}

static const char *_state_str(int st) {
    switch (st) {
        case PROC_READY:   return "READY";
        case PROC_RUNNING: return "RUN";
        case PROC_BLOCKED: return "BLOCK";
        case PROC_ZOMBIE:  return "ZOMBIE";
        default:           return "?";
    }
}

static void _sched_row(sbuf_t *sb, process_t *p) {
    if (!p) return;
    sb_str(sb, "  ");
    sb_u32(sb, p->pid);
    sb_pad(sb, 8);
    sb_str(sb, _state_str(p->state));
    sb_pad(sb, 18);
    sb_str(sb, (const char*)p->name);
    sb_char(sb, '\n');
}

static void _gen_sched(sbuf_t *sb, uint32_t arg) {
    (void)arg;
    sb_str(sb, "  PID     STATE     NAME\n");
    _sched_row(sb, g_current);
    for (process_t *p = g_ready_head;   p; p = p->next) _sched_row(sb, p);
    for (process_t *p = g_blocked_head; p; p = p->next) _sched_row(sb, p);
}

/* /proc/memmap — physical memory heatmap as ANSI-coloured text.
   64x16 grid of full-block glyphs; one cell = a range of frames.   */
#define MM_COLS 64
#define MM_ROWS 16
#define MM_KERNEL_END (0x00500000u / PAGE_SIZE)
enum { MM_FREE = 0, MM_KERNEL, MM_USER, MM_COW };

static void _mm_color(sbuf_t *sb, int st) {
    /* ANSI SGR: 90 grey, 91 red, 92 green, 95 magenta */
    sb_str(sb, "\x1b[");
    switch (st) {
        case MM_COW:    sb_str(sb, "95"); break;
        case MM_USER:   sb_str(sb, "92"); break;
        case MM_KERNEL: sb_str(sb, "91"); break;
        default:        sb_str(sb, "90"); break;
    }
    sb_char(sb, 'm');
}

static void _gen_memmap(sbuf_t *sb, uint32_t arg) {
    (void)arg;
    uint32_t total = pmm_get_total_frames();
    uint32_t cells = MM_COLS * MM_ROWS;
    uint32_t fpc   = (total + cells - 1) / cells;
    if (fpc == 0) fpc = 1;

    int last = -1;
    for (uint32_t row = 0; row < MM_ROWS; row++) {
        for (uint32_t col = 0; col < MM_COLS; col++) {
            uint32_t start = (row * MM_COLS + col) * fpc;
            int st = MM_FREE;
            for (uint32_t f = start; f < start + fpc && f < total; f++) {
                if (!pmm_frame_used(f)) continue;
                int s = (f < MM_KERNEL_END) ? MM_KERNEL
                      : (pmm_ref_count(f * PAGE_SIZE) >= 2) ? MM_COW
                      : MM_USER;
                if (s > st) st = s;
            }
            if (st != last) { _mm_color(sb, st); last = st; }
            sb_char(sb, (char)0xDB);   /* full block █ */
        }
        sb_str(sb, "\x1b[0m\n");
        last = -1;
    }
    /* Legend + summary. */
    uint32_t used = total - pmm_get_free_count();
    sb_str(sb, "\x1b[90m\xDB\x1b[0m free \x1b[91m\xDB\x1b[0m kernel ");
    sb_str(sb, "\x1b[92m\xDB\x1b[0m user \x1b[95m\xDB\x1b[0m cow\n");
    sb_u32(sb, used); sb_str(sb, " / "); sb_u32(sb, total);
    sb_str(sb, " frames used (");
    sb_u32(sb, used * 4 / 1024); sb_str(sb, " / ");
    sb_u32(sb, total * 4 / 1024); sb_str(sb, " MB)\n");
}

static void _gen_uptime(sbuf_t *sb, uint32_t arg) {
    (void)arg;
    uint32_t ms = pit_uptime_ms();
    sb_u32(sb, ms / 1000); sb_char(sb, '.');
    uint32_t frac = ms % 1000;
    sb_char(sb, '0' + (frac / 100));
    sb_char(sb, '0' + (frac / 10) % 10);
    sb_char(sb, '0' + frac % 10);
    sb_str(sb, " s\n");
}

/* ── Node registry ───────────────────────────────────────────── */

static synfs_node_t g_nodes[] = {
    { "/proc/meminfo", SYN_GEN,    _gen_meminfo, 0 },
    { "/proc/slab",    SYN_GEN,    _gen_slab,    0 },
    { "/proc/sched",   SYN_GEN,    _gen_sched,   0 },
    { "/proc/memmap",  SYN_GEN,    _gen_memmap,  0 },
    { "/proc/uptime",  SYN_GEN,    _gen_uptime,  0 },
    { "/dev/null",     SYN_NULL,   (synfs_gen_fn)0, 0 },
    { "/dev/zero",     SYN_ZERO,   (synfs_gen_fn)0, 0 },
    { "/dev/random",   SYN_RANDOM, (synfs_gen_fn)0, 0 },
};
#define N_NODES (sizeof(g_nodes) / sizeof(g_nodes[0]))

void synfs_init(void) { /* static table — nothing to do yet */ }

uint32_t      synfs_count(void)      { return N_NODES; }
synfs_node_t *synfs_at(uint32_t i)   { return i < N_NODES ? &g_nodes[i] : (synfs_node_t*)0; }

static int _streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static int _prefix(const char *s, const char *pfx) {
    while (*pfx) { if (*s++ != *pfx++) return 0; }
    return 1;
}

int synfs_is_synthetic(const char *path) {
    return _prefix(path, "/proc") || _prefix(path, "/dev");
}

synfs_node_t *synfs_lookup(const char *path) {
    for (uint32_t i = 0; i < N_NODES; i++)
        if (_streq(path, g_nodes[i].path)) return &g_nodes[i];
    return (synfs_node_t*)0;
}

/* ── Directory namespace integration ─────────────────────────── */

uint32_t synfs_dir_ino(const char *path) {
    if (_streq(path, "/proc") || _streq(path, "/proc/")) return SYNFS_INO_PROC;
    if (_streq(path, "/dev")  || _streq(path, "/dev/"))  return SYNFS_INO_DEV;
    return 0;
}

int synfs_is_dir_ino(uint32_t ino) {
    return ino == SYNFS_INO_PROC || ino == SYNFS_INO_DEV;
}

/* Copy the basename of a node path (after the last '/') into out. */
static void _basename(const char *path, char *out) {
    const char *p = path, *last = path;
    while (*p) { if (*p == '/') last = p + 1; p++; }
    int i = 0;
    while (last[i] && i < 23) { out[i] = last[i]; i++; }
    out[i] = '\0';
}

int synfs_dir_entry(uint32_t dir_ino, uint32_t idx, char *name, int *is_dir) {
    const char *prefix = (dir_ino == SYNFS_INO_PROC) ? "/proc/"
                       : (dir_ino == SYNFS_INO_DEV)  ? "/dev/"
                       : (const char*)0;
    if (!prefix) return 0;

    uint32_t seen = 0;
    for (uint32_t i = 0; i < N_NODES; i++) {
        if (!_prefix(g_nodes[i].path, prefix)) continue;
        if (seen == idx) {
            _basename(g_nodes[i].path, name);
            if (is_dir) *is_dir = 0;   /* all synthetic nodes are files */
            return 1;
        }
        seen++;
    }
    return 0;
}

/* Top-level synthetic directories injected into the root listing.
   Names carry a leading '/' so they render as "/proc/", "/dev/" —
   visually distinguishing the synthetic mounts from on-disk files. */
static const char *g_root_dirs[] = { "/proc", "/dev" };

uint32_t    synfs_root_dirs(void)            { return 2; }
const char *synfs_root_dir_name(uint32_t i)  { return i < 2 ? g_root_dirs[i] : (const char*)0; }

/* ── Read / write ─────────────────────────────────────────────── */

static uint32_t g_rng = 0x12345678u;
static uint8_t  _rand_byte(void) {
    g_rng = g_rng * 1103515245u + 12345u + pit_uptime_ms();
    return (uint8_t)(g_rng >> 16);
}

int32_t synfs_read(synfs_node_t *n, uint32_t off, uint8_t *buf, uint32_t len) {
    if (!n || !buf) return -1;

    switch (n->kind) {
    case SYN_NULL:
        return 0;                 /* always EOF */
    case SYN_ZERO:
        for (uint32_t i = 0; i < len; i++) buf[i] = 0;
        return (int32_t)len;
    case SYN_RANDOM:
        for (uint32_t i = 0; i < len; i++) buf[i] = _rand_byte();
        return (int32_t)len;
    case SYN_GEN:
    default: break;
    }

    /* Generate full content into a scratch buffer, then slice [off,off+len).
       Single kernel thread services a syscall at a time, so one static
       scratch buffer is safe. */
    static uint8_t scratch[4096];
    sbuf_t sb = { scratch, sizeof(scratch), 0 };
    if (n->gen) n->gen(&sb, n->arg);

    uint32_t total = sb.len < sb.cap ? sb.len : sb.cap;
    if (off >= total) return 0;   /* EOF */
    uint32_t avail = total - off;
    uint32_t n2    = avail < len ? avail : len;
    for (uint32_t i = 0; i < n2; i++) buf[i] = scratch[off + i];
    return (int32_t)n2;
}

int32_t synfs_write(synfs_node_t *n, uint32_t off, const uint8_t *buf, uint32_t len) {
    (void)off; (void)buf;
    if (!n) return -1;
    /* /dev/null and /dev/zero accept and discard; proc files ignore writes. */
    return (int32_t)len;
}

uint32_t synfs_size(synfs_node_t *n) {
    if (!n || n->kind != SYN_GEN) return 0;
    static uint8_t scratch[4096];
    sbuf_t sb = { scratch, sizeof(scratch), 0 };
    if (n->gen) n->gen(&sb, n->arg);
    return sb.len < sb.cap ? sb.len : sb.cap;
}
