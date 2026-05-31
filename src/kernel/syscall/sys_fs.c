/**
 * @file    kernel/syscall/sys_fs.c
 * @brief   Filesystem syscalls: mkdir, chdir, getdents, stat
 */
#include "syscall_internal.h"

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
    uint32_t   base = (path[0] == '/') ? noxfs_root_ino() : proc->cwd_ino;
    uint32_t   ino  = noxfs_resolve(base, path);
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

    if (fd < PROC_MAX_FD && proc->fd_table[fd].used) {
        vfs_file_t* f = proc->fd_table[fd].file;
        if (f && f->inode) dir_ino = f->inode;
    }

    int32_t n = noxfs_getdents(dir_ino, buf, len, off_ptr);
    frame->eax = n >= 0 ? (uint32_t)n : (uint32_t)-1;
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
