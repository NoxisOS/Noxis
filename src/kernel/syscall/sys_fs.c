/**
 * @file    kernel/syscall/sys_fs.c
 * @brief   Filesystem syscalls: mkdir, chdir, getdents, stat
 */
#include "syscall_internal.h"
#include <fs/synfs/synfs.h>

/* Small helpers (no libc in the kernel). */
static int _seq(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

void sys_mkdir(isr_frame_t* frame) {
    const uint8_t* path = (const uint8_t*)frame->ebx;
    if (!path || !_user_range_ok(frame->ebx, 1)) { frame->eax = (uint32_t)-1; return; }

    process_t* proc = scheduler_current();
    uint32_t   base = (path[0] == '/') ? noxfs_root_ino() : proc->cwd_ino;
    uint32_t   ino  = noxfs_mkdir(base, path);
    frame->eax = (ino != (uint32_t)-1) ? 0 : (uint32_t)-1;
}

void sys_chdir(isr_frame_t* frame) {
    const uint8_t* path = (const uint8_t*)frame->ebx;
    if (!path || !_user_range_ok(frame->ebx, 1)) { frame->eax = (uint32_t)-1; return; }

    process_t* proc = scheduler_current();
    const char* p   = (const char*)path;

    /* ── Synthetic directories (/proc, /dev) ───────────────────── */
    uint32_t sino = synfs_dir_ino(p);          /* absolute "/proc","/dev" */
    if (sino) { proc->cwd_ino = sino; frame->eax = 0; return; }

    /* Navigating out of a synthetic dir. */
    if (synfs_is_dir_ino(proc->cwd_ino)) {
        if (_seq(p, "/")) { proc->cwd_ino = noxfs_root_ino(); frame->eax = 0; return; }
        if (_seq(p, "..") || _seq(p, "/..")) {
            /* From /proc/<pid> go up to /proc; from /proc or /dev go to root. */
            if (proc->cwd_ino >= SYNFS_INO_PIDBASE)
                proc->cwd_ino = SYNFS_INO_PROC;
            else
                proc->cwd_ino = noxfs_root_ino();
            frame->eax = 0; return;
        }
        /* Descend from /proc into a <pid> directory: "cd 2". */
        if (proc->cwd_ino == SYNFS_INO_PROC && p[0] != '/') {
            char abs[24]; int i = 0;
            const char* pre = "/proc/";
            while (pre[i]) { abs[i] = pre[i]; i++; }
            int j = 0; while (p[j] && i < 22) abs[i++] = p[j++];
            abs[i] = '\0';
            uint32_t s = synfs_dir_ino(abs);
            if (s) { proc->cwd_ino = s; frame->eax = 0; return; }
        }
        frame->eax = (uint32_t)-1; return;
    }

    /* Relative descent from root into a synthetic dir: "cd proc". */
    if (proc->cwd_ino == noxfs_root_ino() && p[0] != '/') {
        char abs[8]; int i = 0;
        abs[i++] = '/';
        while (p[i-1] && i < 7) { abs[i] = p[i-1]; i++; }
        abs[i] = '\0';
        uint32_t s2 = synfs_dir_ino(abs);
        if (s2) { proc->cwd_ino = s2; frame->eax = 0; return; }
    }

    /* ── Regular NoxFS path ────────────────────────────────────── */
    uint32_t base = (p[0] == '/') ? noxfs_root_ino() : proc->cwd_ino;
    uint32_t ino  = noxfs_resolve(base, path);
    if (ino == (uint32_t)-1) { frame->eax = (uint32_t)-1; return; }

    proc->cwd_ino = ino;
    frame->eax = 0;
}

void sys_getdents(isr_frame_t* frame) {
    uint32_t  fd  = frame->ebx;
    uint8_t*  buf = (uint8_t*)frame->esi;
    uint32_t  len = frame->edi;

    if (!buf || len == 0)              { frame->eax = (uint32_t)-1; return; }
    if (!_user_range_ok(frame->esi, len)) { frame->eax = (uint32_t)-1; return; }

    /* EDX is the sysenter return EIP (always 0 in frame->edx).
       Use it as an offset pointer only when it's a valid user address. */
    uint32_t   local_off = 0;
    uint32_t*  off_ptr   = (frame->edx && _user_range_ok(frame->edx, 4))
                           ? (uint32_t*)frame->edx
                           : &local_off;

    process_t* proc    = scheduler_current();
    uint32_t   dir_ino = proc->cwd_ino;

    if (fd < PROC_MAX_FD && proc->fd_table[fd].used &&
        proc->fd_table[fd].type == FD_FILE) {
        vfs_file_t* f = proc->fd_table[fd].file;
        if (f && f->inode) dir_ino = f->inode;
    }

    /* ── Synthetic directory (/proc, /dev): enumerate synfs nodes ── */
    if (synfs_is_dir_ino(dir_ino)) {
        uint32_t written = 0;
        char     nm[24];
        int      isd;
        for (uint32_t i = 0; synfs_dir_entry(dir_ino, i, nm, &isd); i++) {
            if (written + sizeof(noxfs_dirent_t) > len) break;
            noxfs_dirent_t* d = (noxfs_dirent_t*)(buf + written);
            d->inode     = 0xF0000100u + i;          /* fake nonzero inode */
            d->rec_len   = sizeof(noxfs_dirent_t);
            d->file_type = isd ? NOXFS_FT_DIR : NOXFS_FT_FILE;
            int l = 0; while (nm[l] && l < 23) l++;
            d->name_len = (uint8_t)l;
            for (int j = 0; j < l; j++) d->name[j] = nm[j];
            written += sizeof(noxfs_dirent_t);
        }
        frame->eax = written;
        return;
    }

    int32_t n = noxfs_getdents(dir_ino, buf, len, off_ptr);
    if (n < 0) { frame->eax = (uint32_t)-1; return; }

    /* ── Root: append synthetic top-level dirs (proc, dev) ──────── */
    if (dir_ino == noxfs_root_ino()) {
        uint32_t written = (uint32_t)n;
        for (uint32_t i = 0; i < synfs_root_dirs(); i++) {
            if (written + sizeof(noxfs_dirent_t) > len) break;
            const char* nm = synfs_root_dir_name(i);
            noxfs_dirent_t* d = (noxfs_dirent_t*)(buf + written);
            d->inode     = 0xF0000200u + i;
            d->rec_len   = sizeof(noxfs_dirent_t);
            d->file_type = NOXFS_FT_DIR;
            int l = 0; while (nm[l] && l < 23) l++;
            d->name_len = (uint8_t)l;
            for (int j = 0; j < l; j++) d->name[j] = nm[j];
            written += sizeof(noxfs_dirent_t);
        }
        n = (int32_t)written;
    }

    frame->eax = (uint32_t)n;
}

void sys_stat(isr_frame_t* frame) {
    const uint8_t* path = (const uint8_t*)frame->ebx;
    vfs_file_t*    sb   = (vfs_file_t*)frame->esi;

    if (!path || !sb)                          { frame->eax = (uint32_t)-1; return; }
    if (!_user_range_ok(frame->ebx, 1))           { frame->eax = (uint32_t)-1; return; }
    if (!_user_range_ok(frame->esi, sizeof(vfs_file_t))) { frame->eax = (uint32_t)-1; return; }

    process_t* proc = scheduler_current();
    uint32_t   base = (path[0] == '/') ? noxfs_root_ino() : proc->cwd_ino;
    uint32_t   ino  = noxfs_resolve(base, path);
    if (ino == (uint32_t)-1) { frame->eax = (uint32_t)-1; return; }

    vfs_file_t st;
    if (noxfs_stat(ino, &st) != OS_OK) { frame->eax = (uint32_t)-1; return; }

    sb->size     = st.size;
    sb->inode    = st.inode;
    sb->capacity = st.capacity;
    frame->eax   = 0;
}
