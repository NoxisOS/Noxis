/**
 * @file    proc/signal.c
 * @brief   Minimal signals: kill sets a pending bit; delivery happens at the
 *          next syscall return for the target process.
 *
 * If a user handler is registered we redirect the return to it (pushing the
 * original RIP so the handler can `ret` back); otherwise the default action
 * for terminating signals is to exit the process.
 */
#include <kernel/syscall/syscall.h>
#include <kernel/isr/isr.h>
#include <proc/process.h>
#include <proc/scheduler.h>
#include <common/types.h>

void sys_exit(int code);

/* ── Signal delivery at timer interrupt ────────────────────────────────────
 * Called from scheduler_tick when the current process was interrupted in
 * ring 3 (user mode) and has at least one signal pending.
 *
 * For a user handler: push the interrupted RIP onto the user stack so the
 * handler can `ret` back, then redirect RIP → handler and set RDI = signo.
 * The CPU restores the modified frame via IRET when the interrupt returns.
 *
 * For the default action (fatal signals): call sys_exit; never returns.
 */
void deliver_signals_isr(isr_frame_t* frame) {
    process_t* p = scheduler_current();
    if (!p || !p->sig_pending) return;

    for (int sig = 1; sig < 32; sig++) {
        if (!(p->sig_pending & (1u << sig))) continue;
        p->sig_pending &= ~(1u << sig);

        uint64_t h = p->sig_handler[sig];
        if (h) {
            /* Push return address on the user stack, redirect to handler. */
            frame->rsp -= 8;
            *(uint64_t*)frame->rsp = frame->rip;
            frame->rip = h;
            frame->rdi = (uint64_t)sig;
            return;   /* one signal per interrupt */
        }
        if (sig == SIGKILL || sig == SIGTERM || sig == SIGINT)
            sys_exit(128 + sig);   /* never returns */
    }
}

/* signal(sig, handler): install a handler (0 = default). Returns 0/-1. */
int64_t sys_signal(int sig, uint64_t handler) {
    if (sig <= 0 || sig >= 32) return -1;
    scheduler_current()->sig_handler[sig] = handler;
    return 0;
}

/* kill(pid, sig): mark the signal pending on the target process. */
int64_t sys_kill(int64_t pid, int sig) {
    if (sig <= 0 || sig >= 32) return -1;
    process_t* t = scheduler_find((uint64_t)pid);
    if (!t) return -1;
    t->sig_pending |= (1u << sig);
    return 0;
}

/* Called at every syscall return: deliver pending signals to the caller. */
void deliver_signals(syscall_frame_t* f) {
    process_t* p = scheduler_current();
    if (!p->sig_pending) return;

    for (int sig = 1; sig < 32; sig++) {
        if (!(p->sig_pending & (1u << sig))) continue;
        p->sig_pending &= ~(1u << sig);

        uint64_t h = p->sig_handler[sig];
        if (h) {
            /* Run the user handler: push the current RIP as its return address
               (we are in the process's AS, so the user stack is mapped), then
               enter the handler with signo in RDI. */
            f->ursp -= 8;
            *(uint64_t*)f->ursp = f->rip;
            f->rip = h;
            f->rdi = (uint64_t)sig;
            return;                       /* one signal per return */
        }
        /* Default action: terminate on the usual fatal signals. */
        if (sig == SIGKILL || sig == SIGTERM || sig == SIGINT)
            sys_exit(128 + sig);          /* never returns */
    }
}
