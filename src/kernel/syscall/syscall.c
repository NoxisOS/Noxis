/**
 * @file    kernel/syscall/syscall.c
 * @brief   Syscall dispatcher and initialisation (int 0x80 + sysenter).
 *
 *  This file is intentionally thin: it only contains the dispatch switch
 *  and the hardware setup.  All handler implementations live in:
 *
 *    sys_io.c      — write, read, ioctl
 *    sys_fd.c      — open, creat, close, dup, dup2, lseek, pipe
 *    sys_fs.c      — mkdir, chdir, getdents, stat
 *    sys_proc.c    — exit, fork, waitpid, execve, brk, getpid, getppid, getuid
 *    sys_signal.c  — sigaction, kill, sigreturn, sigprocmask, signal_deliver
 *    sys_misc.c    — time, sleep
 */
#include "syscall_internal.h"

extern void isr_stub_128(void);
extern void sysenter_entry(void);

/* ── dispatch ───────────────────────────────────────────────── */

static void _syscall_dispatch(isr_frame_t* frame) {
    switch (frame->eax) {
    case SYS_EXIT:        sys_exit       (frame); break;
    case SYS_WRITE:       sys_write      (frame); break;
    case SYS_READ:        sys_read       (frame); break;
    case SYS_OPEN:        sys_open       (frame); break;
    case SYS_CLOSE:       sys_close      (frame); break;
    case SYS_FORK:        sys_fork       (frame); break;
    case SYS_WAITPID:     sys_waitpid    (frame); break;
    case SYS_CREAT:       sys_creat      (frame); break;
    case SYS_PIPE:        sys_pipe       (frame); break;
    case SYS_DUP:         sys_dup        (frame); break;
    case SYS_SIGACTION:   sys_sigaction  (frame); break;
    case SYS_KILL:        sys_kill       (frame); break;
    case SYS_GETPID:      sys_getpid     (frame); break;
    case SYS_IOCTL:       sys_ioctl      (frame); break;
    case SYS_MKDIR:       sys_mkdir      (frame); break;
    case SYS_CHDIR:       sys_chdir      (frame); break;
    case SYS_GETDENTS:    sys_getdents   (frame); break;
    case SYS_STAT:        sys_stat       (frame); break;
    case SYS_LSEEK:       sys_lseek      (frame); break;
    case SYS_EXECVE:      sys_execve     (frame); break;
    case SYS_BRK:         sys_brk        (frame); break;
    case SYS_GETPPID:     sys_getppid    (frame); break;
    case SYS_GETUID:      sys_getuid     (frame); break;
    case SYS_TIME:        sys_time       (frame); break;
    case SYS_DUP2:        sys_dup2       (frame); break;
    case SYS_SLEEP:       sys_sleep      (frame); break;
    case SYS_SIGRETURN:   sys_sigreturn  (frame); break;
    case SYS_SIGPROCMASK: sys_sigprocmask(frame); break;
    case SYS_UNLINK:      sys_unlink     (frame); break;
    case SYS_RENAME:      sys_rename     (frame); break;
    default: break;
    }
}

/* ── public entry points ────────────────────────────────────── */

void syscall_handler(isr_frame_t* frame) {
    _syscall_dispatch(frame);
    signal_deliver(frame);
}

os_status_t syscall_init(void) {
    isr_register_handler(128, syscall_handler);
    idt_set_gate(128, (uint32_t)isr_stub_128,
                 IDT_PRESENT | IDT_DPL3 | IDT_GATE_INT32);
    msr_write(MSR_SYSENTER_CS,  0x08, 0);
    msr_write(MSR_SYSENTER_ESP, scheduler_current()->kstack_top, 0);
    msr_write(MSR_SYSENTER_EIP, (uint32_t)sysenter_entry, 0);
    return OS_OK;
}
