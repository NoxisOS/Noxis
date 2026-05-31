/**
 * @file    kernel/syscall/sys_signal.c
 * @brief   Signal syscalls: sigaction, kill, sigreturn, sigprocmask
 *          + signal_deliver (called by syscall_handler after every dispatch)
 */
#include "syscall_internal.h"

/* ── sigaction ──────────────────────────────────────────────── */

void sys_sigaction(isr_frame_t* frame) {
    uint32_t     signum = frame->ebx;
    sigaction_t* act    = (sigaction_t*)frame->esi;

    if (signum >= NSIG || signum == 0)              { frame->eax = (uint32_t)-1; return; }
    if (signum == SIGKILL || signum == SIGSTOP)     { frame->eax = (uint32_t)-1; return; }

    if (act) {
        if (!_user_range_ok(frame->esi, sizeof(sigaction_t)))
            { frame->eax = (uint32_t)-1; return; }
        process_t* proc = scheduler_current();
        proc->sigactions[signum].handler  = act->handler;
        proc->sigactions[signum].flags    = act->flags;
        proc->sigactions[signum].restorer = act->restorer;
    }
    frame->eax = 0;
}

/* ── kill ───────────────────────────────────────────────────── */

void sys_kill(isr_frame_t* frame) {
    uint32_t pid = frame->ebx;
    uint32_t sig = frame->esi;

    if (sig >= NSIG || sig == 0) { frame->eax = (uint32_t)-1; return; }

    process_t* target = scheduler_find_proc(pid);
    if (!target)                 { frame->eax = (uint32_t)-1; return; }

    /* SIGKILL cannot be caught — reset to default if overridden. */
    if (sig == SIGKILL && target->sigactions[SIGKILL].handler != SIG_DFL)
        target->sigactions[SIGKILL].handler = SIG_DFL;

    target->sig_pending |= (1u << sig);
    frame->eax = 0;
}

/* ── sigreturn ──────────────────────────────────────────────── */
/*
 * Signal stack layout at int-0x80 entry (frame->user_esp):
 *   [+0]  sig number   (handler's `ret` advanced past the restorer_addr slot)
 *   [+4]  sig_ucontext_t  (10 × 4 = 40 bytes)
 *
 * The kernel restores all registers and EFLAGS from the ucontext, then
 * the int-0x80 iret path returns straight to the interrupted instruction.
 */
void sys_sigreturn(isr_frame_t* frame) {
    uint32_t uc_addr = frame->user_esp + 4;
    if (!_user_range_ok(uc_addr, sizeof(sig_ucontext_t))) return;

    sig_ucontext_t* uc = (sig_ucontext_t*)uc_addr;

    frame->eax      = uc->eax;
    frame->ecx      = uc->ecx;
    frame->edx      = uc->edx;
    frame->ebx      = uc->ebx;
    frame->esi      = uc->esi;
    frame->edi      = uc->edi;
    frame->ebp      = uc->ebp;
    frame->eip      = uc->eip;
    frame->eflags   = (uc->eflags & ~0x200u) | 0x200u;  /* keep IF=1 */
    frame->user_esp = uc->esp;
}

/* ── sigprocmask ────────────────────────────────────────────── */

#define _SIG_BLOCK   0
#define _SIG_UNBLOCK 1
#define _SIG_SETMASK 2

void sys_sigprocmask(isr_frame_t* frame) {
    uint32_t  how  = frame->ebx;
    uint32_t* nset = (uint32_t*)frame->esi;
    uint32_t* oset = (uint32_t*)frame->edi;

    process_t* proc = scheduler_current();

    if (oset) {
        if (!_user_range_ok(frame->edi, 4)) { frame->eax = (uint32_t)-1; return; }
        *oset = proc->sig_blocked;
    }
    if (nset) {
        if (!_user_range_ok(frame->esi, 4)) { frame->eax = (uint32_t)-1; return; }
        uint32_t mask = *nset & ~((1u << SIGKILL) | (1u << SIGSTOP));
        switch (how) {
        case _SIG_BLOCK:   proc->sig_blocked |=  mask; break;
        case _SIG_UNBLOCK: proc->sig_blocked &= ~mask; break;
        case _SIG_SETMASK: proc->sig_blocked  =  mask; break;
        default: frame->eax = (uint32_t)-1; return;
        }
    }
    frame->eax = 0;
}

/* ── signal_deliver ─────────────────────────────────────────── */
/*
 * Signal frame pushed on the user stack (48 bytes total):
 *
 *   new_esp +  0 : restorer_addr   ← handler's return address
 *   new_esp +  4 : sig             ← handler argument (int sig)
 *   new_esp +  8 : sig_ucontext_t  ← 10 × 4 bytes of saved CPU state
 *
 * After handler `ret`  → ESP = new_esp + 4, EIP = restorer_addr
 * Restorer: int $0x80 (SYS_SIGRETURN) → sys_sigreturn restores everything.
 */
#define SIG_FRAME_SIZE  48u

void signal_deliver(isr_frame_t* frame) {
    process_t* cur = scheduler_current();
    if (!cur || cur->page_dir_phys == 0) return;

    uint32_t pending = cur->sig_pending & ~cur->sig_blocked;
    if (!pending) return;

    for (uint32_t sig = 1; sig < NSIG; sig++) {
        if (!(pending & (1u << sig))) continue;
        cur->sig_pending &= ~(1u << sig);

        sighandler_t handler = cur->sigactions[sig].handler;

        /* ── Default action ─────────────────────────────────── */
        if (handler == SIG_DFL) {
            switch (sig) {
            case SIGKILL: case SIGTERM: case SIGINT:  case SIGQUIT:
            case SIGSEGV: case SIGILL:  case SIGFPE:  case SIGBUS:
            case SIGABRT:
                cur->exit_code = 128 + (int32_t)sig;
                if (cur->is_fork_child) {
                    __asm__ __volatile__("cli");
                    cur->state = PROC_ZOMBIE;
                    if (cur->ppid) {
                        process_t* parent = scheduler_find_proc(cur->ppid);
                        if (parent)
                            parent->sig_pending |= (1u << SIGCHLD);
                    }
                    if (cur->waiter) {
                        scheduler_add(cur->waiter);
                        cur->waiter = (process_t*)0;
                    }
                    vfs_sync();
                    scheduler_exit();
                } else {
                    vfs_sync();
                    exec_return((int)cur->exit_code);
                }
                break;
            default:
                break;
            }
            return;
        }

        /* ── Ignored ─────────────────────────────────────────── */
        if (handler == SIG_IGN) continue;

        /* ── Custom handler ──────────────────────────────────── */
        if (!(cur->sigactions[sig].flags & SA_RESTORER) ||
            !cur->sigactions[sig].restorer) {
            cur->sigactions[sig].handler = SIG_DFL;
            cur->sig_pending |= (1u << sig);
            continue;
        }

        uint32_t uesp = frame->user_esp;
        if (uesp < USER_VIRT_BASE + SIG_FRAME_SIZE || uesp > USER_VIRT_TOP)
            return;

        uesp -= SIG_FRAME_SIZE;
        uint32_t* f = (uint32_t*)uesp;

        f[0]  = (uint32_t)cur->sigactions[sig].restorer;
        f[1]  = sig;
        f[2]  = frame->eax;
        f[3]  = frame->ecx;
        f[4]  = frame->edx;
        f[5]  = frame->ebx;
        f[6]  = frame->esi;
        f[7]  = frame->edi;
        f[8]  = frame->ebp;
        f[9]  = frame->eip;
        f[10] = frame->eflags;
        f[11] = frame->user_esp;

        frame->user_esp = uesp;
        frame->eip      = (uint32_t)handler;
        return;
    }
}
