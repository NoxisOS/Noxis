/**
 * @file    kernel/syscall/sys_io.c
 * @brief   I/O syscalls: write, read, ioctl
 */
#include "syscall_internal.h"
#include <fs/synfs/synfs.h>

void sys_write(isr_frame_t* frame) {
    uint32_t        fd  = frame->ebx;
    const uint8_t*  buf = (const uint8_t*)frame->esi;
    uint32_t        len = frame->edi;

    if (len == 0) return;
    if (!_user_range_ok(frame->esi, len)) { frame->eax = (uint32_t)-1; return; }

    /* stdout / stderr → VGA (with ANSI escape support) */
    if (fd == STDOUT_FD || fd == STDERR_FD || fd == 0) {
        vga_ansi_write(buf, len);
        frame->eax = len;
        return;
    }

    process_t* proc = scheduler_current();

    /* Pipe-backed fd */
    if (fd < PROC_MAX_FD && proc->fd_table[fd].used &&
        proc->fd_table[fd].type == FD_PIPE) {
        int32_t n = pipe_write(proc->fd_table[fd].pipe, buf, len);
        frame->eax = n >= 0 ? (uint32_t)n : (uint32_t)-1;
        return;
    }

    /* Synthetic file (/proc, /dev) */
    if (fd < PROC_MAX_FD && proc->fd_table[fd].used &&
        proc->fd_table[fd].type == FD_SYN) {
        int32_t n = synfs_write(proc->fd_table[fd].syn,
                                proc->fd_table[fd].pos, buf, len);
        if (n > 0) proc->fd_table[fd].pos += (uint32_t)n;
        frame->eax = n >= 0 ? (uint32_t)n : (uint32_t)-1;
        return;
    }

    /* File-backed fd */
    if (fd < PROC_MAX_FD && proc->fd_table[fd].used) {
        vfs_file_t* f = proc->fd_table[fd].file;
        int32_t n = vfs_write_file(f, proc->fd_table[fd].pos, buf, len);
        if (n > 0) proc->fd_table[fd].pos += (uint32_t)n;
        frame->eax = n > 0 ? (uint32_t)n : (uint32_t)-1;
        return;
    }

    frame->eax = (uint32_t)-1;
}

void sys_read(isr_frame_t* frame) {
    uint32_t  fd     = frame->ebx;
    uint8_t*  buf    = (uint8_t*)frame->esi;
    uint32_t  maxlen = frame->edi;

    if (maxlen == 0 || !buf)               { frame->eax = (uint32_t)-1; return; }
    if (!_user_range_ok(frame->esi, maxlen)) { frame->eax = (uint32_t)-1; return; }

    process_t* proc = scheduler_current();

    /* Pipe-backed fd */
    if (fd < PROC_MAX_FD && proc->fd_table[fd].used &&
        proc->fd_table[fd].type == FD_PIPE) {
        int32_t n = pipe_read(proc->fd_table[fd].pipe, buf, maxlen);
        frame->eax = n >= 0 ? (uint32_t)n : (uint32_t)-1;
        return;
    }

    /* Synthetic file (/proc, /dev) */
    if (fd < PROC_MAX_FD && proc->fd_table[fd].used &&
        proc->fd_table[fd].type == FD_SYN) {
        int32_t n = synfs_read(proc->fd_table[fd].syn,
                               proc->fd_table[fd].pos, buf, maxlen);
        if (n > 0) proc->fd_table[fd].pos += (uint32_t)n;
        frame->eax = n >= 0 ? (uint32_t)n : (uint32_t)-1;
        return;
    }

    /* File-backed fd */
    if (fd < PROC_MAX_FD && proc->fd_table[fd].used &&
        proc->fd_table[fd].type == FD_FILE) {
        vfs_file_t* f  = proc->fd_table[fd].file;
        uint32_t    pos = proc->fd_table[fd].pos;
        uint32_t    rem = f->size - pos;
        uint32_t    n   = rem < maxlen ? rem : maxlen;

        for (uint32_t i = 0; i < n; i++)
            buf[i] = f->data[pos + i];

        proc->fd_table[fd].pos += n;
        frame->eax = n;
        return;
    }

    /* stdin → TTY */
    if (fd == STDIN_FD) {
        int32_t n = tty_read(buf, maxlen);
        frame->eax = n >= 0 ? (uint32_t)n : (uint32_t)-1;
        return;
    }

    frame->eax = (uint32_t)-1;
}

void sys_ioctl(isr_frame_t* frame) {
    uint32_t fd  = frame->ebx;
    uint32_t req = frame->esi;
    void*    arg = (void*)frame->edi;

    if (fd == STDIN_FD) {
        if (arg && !_user_range_ok(frame->edi, sizeof(termios_t)))
            { frame->eax = (uint32_t)-1; return; }
        frame->eax = (uint32_t)tty_ioctl(req, arg);
        return;
    }
    frame->eax = (uint32_t)-1;
}
