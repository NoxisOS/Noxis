/**
 * @file    kernel/syscall/sys_fd.c
 * @brief   File-descriptor syscalls: open, creat, close, dup, dup2, lseek, pipe
 */
#include "syscall_internal.h"
#include <fs/synfs/synfs.h>

void sys_open(isr_frame_t* frame) {
    const uint8_t* name = (const uint8_t*)frame->ebx;
    if (!name || !_user_range_ok(frame->ebx, 1)) { frame->eax = (uint32_t)-1; return; }

    process_t* proc = scheduler_current();

    /* Build an absolute path for synthetic lookups: if the cwd is a
       synthetic directory and the name is relative, prepend its prefix
       (so `cd /proc; cat meminfo` resolves to /proc/meminfo).         */
    char        synpath[64];
    const char* lookpath = (const char*)name;
    if (name[0] != '/' && synfs_is_dir_ino(proc->cwd_ino)) {
        const char* pfx = (proc->cwd_ino == SYNFS_INO_PROC) ? "/proc/" : "/dev/";
        int k = 0;
        while (pfx[k] && k < 62) { synpath[k] = pfx[k]; k++; }
        int j = 0;
        while (name[j] && k < 63) { synpath[k++] = name[j++]; }
        synpath[k] = '\0';
        lookpath = synpath;
    }

    /* ── Synthetic files (/proc, /dev) take priority ───────────── */
    if (synfs_is_synthetic(lookpath)) {
        synfs_node_t* sn = synfs_lookup(lookpath);
        if (!sn) { frame->eax = (uint32_t)-1; return; }
        for (uint32_t i = 3; i < PROC_MAX_FD; i++) {
            if (!proc->fd_table[i].used) {
                proc->fd_table[i].type = FD_SYN;
                proc->fd_table[i].syn  = sn;
                proc->fd_table[i].pos  = 0;
                proc->fd_table[i].used = TRUE;
                frame->eax = i;
                return;
            }
        }
        frame->eax = (uint32_t)-1;
        return;
    }

    /* ── Regular files (NoxFS / ramfs) ─────────────────────────── */
    vfs_file_t* f = vfs_lookup(name);
    if (!f) { frame->eax = (uint32_t)-1; return; }

    for (uint32_t i = 3; i < PROC_MAX_FD; i++) {
        if (!proc->fd_table[i].used) {
            proc->fd_table[i].type = FD_FILE;
            proc->fd_table[i].file = f;
            proc->fd_table[i].pos  = 0;
            proc->fd_table[i].used = TRUE;
            frame->eax = i;
            return;
        }
    }
    frame->eax = (uint32_t)-1;
}

void sys_creat(isr_frame_t* frame) {
    const uint8_t* name = (const uint8_t*)frame->ebx;
    if (!name || !_user_range_ok(frame->ebx, 1)) { frame->eax = (uint32_t)-1; return; }

    vfs_file_t* f = vfs_creat(name);
    if (!f) { frame->eax = (uint32_t)-1; return; }

    process_t* proc = scheduler_current();
    for (uint32_t i = 3; i < PROC_MAX_FD; i++) {
        if (!proc->fd_table[i].used) {
            proc->fd_table[i].file = f;
            proc->fd_table[i].pos  = 0;
            proc->fd_table[i].used = TRUE;
            frame->eax = i;
            return;
        }
    }
    frame->eax = (uint32_t)-1;
}

void sys_close(isr_frame_t* frame) {
    uint32_t fd = frame->ebx;
    if (fd >= PROC_MAX_FD) { frame->eax = (uint32_t)-1; return; }

    process_t* proc = scheduler_current();
    if (!proc->fd_table[fd].used) { frame->eax = (uint32_t)-1; return; }

    if (proc->fd_table[fd].type == FD_PIPE && proc->fd_table[fd].pipe)
        pipe_close(proc->fd_table[fd].pipe);

    proc->fd_table[fd].type = FD_FILE;
    proc->fd_table[fd].used = FALSE;
    proc->fd_table[fd].file = (void*)0;
    proc->fd_table[fd].pos  = 0;
    frame->eax = 0;
}

void sys_dup(isr_frame_t* frame) {
    uint32_t   oldfd = frame->ebx;
    process_t* proc  = scheduler_current();

    if (oldfd >= PROC_MAX_FD || !proc->fd_table[oldfd].used)
        { frame->eax = (uint32_t)-1; return; }

    for (uint32_t i = 3; i < PROC_MAX_FD; i++) {
        if (!proc->fd_table[i].used) {
            proc->fd_table[i].type = proc->fd_table[oldfd].type;
            proc->fd_table[i].file = proc->fd_table[oldfd].file;
            proc->fd_table[i].pos  = 0;
            proc->fd_table[i].used = TRUE;
            frame->eax = i;
            return;
        }
    }
    frame->eax = (uint32_t)-1;
}

void sys_dup2(isr_frame_t* frame) {
    uint32_t   oldfd = frame->ebx;
    uint32_t   newfd = frame->esi;
    process_t* proc  = scheduler_current();

    if (oldfd >= PROC_MAX_FD || newfd >= PROC_MAX_FD ||
        !proc->fd_table[oldfd].used)  { frame->eax = (uint32_t)-1; return; }
    if (oldfd == newfd)               { frame->eax = newfd; return; }

    /* Close whatever currently occupies newfd */
    if (proc->fd_table[newfd].used &&
        proc->fd_table[newfd].type == FD_PIPE && proc->fd_table[newfd].pipe)
        pipe_close(proc->fd_table[newfd].pipe);

    proc->fd_table[newfd]      = proc->fd_table[oldfd];
    proc->fd_table[newfd].pos  = 0;
    proc->fd_table[newfd].used = TRUE;

    /* A duplicated pipe end adds a reference */
    if (proc->fd_table[newfd].type == FD_PIPE && proc->fd_table[newfd].pipe)
        proc->fd_table[newfd].pipe->refs++;

    frame->eax = newfd;
}

void sys_lseek(isr_frame_t* frame) {
    uint32_t   fd     = frame->ebx;
    int32_t    offset = (int32_t)frame->esi;
    uint32_t   whence = frame->edi;
    process_t* proc   = scheduler_current();

    if (fd >= PROC_MAX_FD || !proc->fd_table[fd].used)
        { frame->eax = (uint32_t)-1; return; }

    switch (whence) {
    case 0: /* SEEK_SET */
        if (offset < 0) { frame->eax = (uint32_t)-1; return; }
        proc->fd_table[fd].pos = (uint32_t)offset;
        break;
    case 1: /* SEEK_CUR */
        if (offset < 0 && (uint32_t)(-offset) > proc->fd_table[fd].pos)
            { frame->eax = (uint32_t)-1; return; }
        proc->fd_table[fd].pos =
            (uint32_t)((int32_t)proc->fd_table[fd].pos + offset);
        break;
    case 2: { /* SEEK_END */
        uint32_t end;
        if (proc->fd_table[fd].type == FD_SYN)
            end = synfs_size(proc->fd_table[fd].syn);
        else {
            vfs_file_t* f = proc->fd_table[fd].file;
            end = f ? f->size : 0;
        }
        if (offset < 0 && (uint32_t)(-offset) > end)
            { frame->eax = (uint32_t)-1; return; }
        proc->fd_table[fd].pos = (uint32_t)((int32_t)end + offset);
        break;
    }
    default:
        frame->eax = (uint32_t)-1; return;
    }
    frame->eax = proc->fd_table[fd].pos;
}

void sys_pipe(isr_frame_t* frame) {
    /* EBX = pointer to user-space int fd[2] */
    if (!_user_range_ok(frame->ebx, 8)) { frame->eax = (uint32_t)-1; return; }

    pipe_t* p = pipe_alloc();
    if (!p) { frame->eax = (uint32_t)-1; return; }

    process_t* proc     = scheduler_current();
    int32_t    fd_read  = -1;
    int32_t    fd_write = -1;

    for (uint32_t i = 3; i < PROC_MAX_FD; i++) {
        if (!proc->fd_table[i].used) {
            if (fd_read < 0) {
                fd_read = (int32_t)i;
                proc->fd_table[i].type = FD_PIPE;
                proc->fd_table[i].pipe = p;
                proc->fd_table[i].pos  = 0;
                proc->fd_table[i].used = TRUE;
            } else if (fd_write < 0) {
                fd_write = (int32_t)i;
                proc->fd_table[i].type = FD_PIPE;
                proc->fd_table[i].pipe = p;
                proc->fd_table[i].pos  = 0;
                proc->fd_table[i].used = TRUE;
                break;
            }
        }
    }

    if (fd_write < 0) {
        if (fd_read >= 0) {
            proc->fd_table[fd_read].used = FALSE;
            proc->fd_table[fd_read].type = FD_FILE;
            proc->fd_table[fd_read].file = (void*)0;
        }
        pipe_close(p);
        frame->eax = (uint32_t)-1;
        return;
    }

    uint32_t* user_fds = (uint32_t*)frame->ebx;
    user_fds[0] = (uint32_t)fd_read;
    user_fds[1] = (uint32_t)fd_write;
    frame->eax  = 0;
}
