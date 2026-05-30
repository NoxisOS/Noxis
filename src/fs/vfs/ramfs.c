/**
 * @file    fs/vfs/ramfs.c
 * @brief   In-memory filesystem backend — static, read-only.
 * @author  Noxis Team
 * @date    2026-05-30
 */
#include <fs/vfs/vfs.h>

/* ── canned file content ────────────────────────────────────── */

static const uint8_t _motd[] =
    "Welcome to Noxis OS.\n"
    "A tiny x86 kernel, written from scratch.\n"
    "Type 'help' for commands.\n";

static const uint8_t _version[] =
    "noxis 0.6.0\n"
    "built 2026-05-30\n";

static const uint8_t _readme[] =
    "Noxis is an educational kernel.\n"
    "Boot -> GDT/IDT -> paging -> heap -> PIT -> KBD -> sched.\n"
    "User mode reached via sysenter. Ring 3 demo printed 'Hello'.\n";

/* ── file table ─────────────────────────────────────────────── */

static vfs_file_t _files[] = {
    { "motd",    (uint8_t*)_motd,    sizeof(_motd)    - 1, 0, 0 },
    { "version", (uint8_t*)_version, sizeof(_version) - 1, 0, 0 },
    { "readme",  (uint8_t*)_readme,  sizeof(_readme)  - 1, 0, 0 },
};

#define FILES_N (sizeof(_files) / sizeof(_files[0]))

/* ── private ────────────────────────────────────────────────── */

static int _streq(const uint8_t* a, const uint8_t* b) {
    uint32_t i = 0;
    while (a[i] && b[i]) { if (a[i] != b[i]) return 0; i++; }
    return a[i] == b[i];
}

/* ── ramfs API (consumed by vfs.c via extern) ───────────────── */

os_status_t ramfs_init(void) { return OS_OK; }
uint32_t    ramfs_count(void) { return FILES_N; }

vfs_file_t* ramfs_entry(uint32_t i) {
    return i < FILES_N ? &_files[i] : (vfs_file_t*)0;
}

vfs_file_t* ramfs_lookup(const uint8_t* name) {
    for (uint32_t i = 0; i < FILES_N; i++) {
        if (_streq(_files[i].name, name)) return &_files[i];
    }
    return (vfs_file_t*)0;
}
