/**
 * @file    kernel/syscall/syscall_internal.h
 * @brief   Internal header shared by all sys_*.c translation units.
 *
 *  - Common includes (isr, process, scheduler, …)
 *  - User-pointer validation helper
 *  - Forward declarations for every handler
 *
 *  This header is NOT part of the public kernel API; only the files
 *  inside kernel/syscall/ should include it.
 */
#ifndef KERNEL_SYSCALL_INTERNAL_H
#define KERNEL_SYSCALL_INTERNAL_H

#include <kernel/syscall/syscall.h>
#include <kernel/isr/isr.h>
#include <kernel/hal/idt.h>
#include <kernel/hal/gdt.h>
#include <proc/scheduler.h>
#include <proc/process.h>
#include <proc/exec.h>
#include <proc/signal.h>
#include <drivers/vga.h>
#include <drivers/tty/tty.h>
#include <drivers/pit.h>
#include <fs/vfs/vfs.h>
#include <fs/noxfs/noxfs.h>
#include <fs/pipe/pipe.h>
#include <common/types.h>
#include <common/signal.h>
#include <mm/virt/vmm.h>
#include <mm/virt/paging.h>
#include <mm/virt/uvm.h>
#include <mm/phys/pmm.h>
#include <proc/elf.h>

/* ── MSR numbers ────────────────────────────────────────────── */
#define MSR_SYSENTER_CS   0x174u
#define MSR_SYSENTER_ESP  0x175u
#define MSR_SYSENTER_EIP  0x176u

/* ── User-space virtual address bounds ──────────────────────── */
#define USER_VIRT_BASE  0x00400000u
#define USER_VIRT_TOP   0xC0000000u

/* ── User-pointer range check ───────────────────────────────── */
static inline int _user_range_ok(uint32_t ptr, uint32_t len) {
    if (ptr < USER_VIRT_BASE)        return 0;
    if (ptr + len < ptr)             return 0;   /* overflow */
    if (ptr + len > USER_VIRT_TOP)   return 0;
    return 1;
}

/* ── External ASM helpers ───────────────────────────────────── */
extern void msr_write(uint32_t msr, uint32_t low, uint32_t high);
extern void kthread_switch(uint32_t* old_esp, uint32_t* new_esp);
extern void user_enter(uint32_t entry, uint32_t stack);

/* ── Scheduler internals (waitpid inline blocking) ──────────── */
extern process_t* g_current;
extern process_t* g_ready_head;
extern process_t* g_ready_tail;

/* ── Handler declarations ───────────────────────────────────── */

/* sys_io.c */
void sys_write     (isr_frame_t* frame);
void sys_read      (isr_frame_t* frame);
void sys_ioctl     (isr_frame_t* frame);

/* sys_fd.c */
void sys_open      (isr_frame_t* frame);
void sys_creat     (isr_frame_t* frame);
void sys_close     (isr_frame_t* frame);
void sys_dup       (isr_frame_t* frame);
void sys_dup2      (isr_frame_t* frame);
void sys_lseek     (isr_frame_t* frame);
void sys_pipe      (isr_frame_t* frame);

/* sys_fs.c */
void sys_mkdir     (isr_frame_t* frame);
void sys_chdir     (isr_frame_t* frame);
void sys_getdents  (isr_frame_t* frame);
void sys_stat      (isr_frame_t* frame);

/* sys_proc.c */
void sys_exit      (isr_frame_t* frame);
void sys_fork      (isr_frame_t* frame);
void sys_waitpid   (isr_frame_t* frame);
void sys_execve    (isr_frame_t* frame);
void sys_brk       (isr_frame_t* frame);
void sys_getpid    (isr_frame_t* frame);
void sys_getppid   (isr_frame_t* frame);
void sys_getuid    (isr_frame_t* frame);

/* sys_signal.c */
void sys_sigaction  (isr_frame_t* frame);
void sys_kill       (isr_frame_t* frame);
void sys_sigreturn  (isr_frame_t* frame);
void sys_sigprocmask(isr_frame_t* frame);

/* sys_misc.c */
void sys_time      (isr_frame_t* frame);
void sys_sleep     (isr_frame_t* frame);

#endif /* KERNEL_SYSCALL_INTERNAL_H */
