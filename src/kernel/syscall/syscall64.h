/**
 * @file    kernel/syscall/syscall64.h
 * @brief   x86-64 syscall trapframe + dispatcher interface.
 *
 * syscall_entry.asm captures the full user register state into a
 * syscall_frame_t on the current process's kernel stack, so fork() can clone
 * it and exec()/signals can rewrite it. Field order MUST match the push order
 * in syscall_entry.asm (rax pushed last → lowest address → first field).
 */
#ifndef KERNEL_SYSCALL64_H
#define KERNEL_SYSCALL64_H

#include <common/types.h>
#include <common/status.h>

typedef struct {
    uint64_t rax, rdi, rsi, rdx, r10, r8, r9;   /* num + args (caller-saved) */
    uint64_t rbx, rbp, r12, r13, r14, r15;       /* callee-saved (preserved)  */
    uint64_t rip, rflags, ursp;                  /* sysret target context     */
} syscall_frame_t;

os_status_t syscall_init(void);
void        syscall_dispatch(syscall_frame_t* f);   /* result written to f->rax */

/* asm: resume a process in ring 3 from a syscall_frame (used by fork child). */
extern void fork_ret_trampoline(void);

/* Updated by the scheduler: top of the current process's kernel stack. */
extern uint64_t g_cur_kstack;

#endif /* KERNEL_SYSCALL64_H */
