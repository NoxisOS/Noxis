/**
 * @file    noxlib/sys/signal.c
 * @brief   signal() / sigaction() / raise() user-space wrappers
 *
 * The kernel's sigaction_t has exactly three fields: handler, flags,
 * restorer (12 bytes, matching common/signal.h).  Our struct sigaction
 * adds sa_mask as a 4th field which the kernel ignores, so we copy
 * only the first 12 bytes when calling the raw syscall.
 *
 * @author  Noxis Team
 * @date    2026-05-31
 */
#include <signal.h>
#include <unistd.h>

/*
 * Kernel-side sigaction struct (12 bytes, matches common/signal.h).
 * The asm 'sigaction' wrapper (unistd.h) takes void* so any pointer
 * converts implicitly — no conflicting declaration needed here.
 */
typedef struct {
    sighandler_t  handler;
    uint32_t      flags;
    void        (*restorer)(void);
} _kern_sa_t;

/* ── signal() ──────────────────────────────────────────────── */

sighandler_t signal(int sig, sighandler_t handler)
{
    if (sig <= 0 || sig >= NSIG) return SIG_ERR;

    _kern_sa_t new_act;
    _kern_sa_t old_act;

    new_act.handler  = handler;
    new_act.flags    = 0;
    new_act.restorer = (void(*)(void))0;

    if (handler != SIG_DFL && handler != SIG_IGN) {
        new_act.flags    = SA_RESTORER;
        new_act.restorer = __sig_restorer;
    }

    /* sigaction(sig, void*, void*) is in unistd.h; kernel reads 12 bytes. */
    if (sigaction(sig, &new_act, &old_act) != 0)
        return SIG_ERR;

    return old_act.handler;
}

/* ── raise() ───────────────────────────────────────────────── */

int raise(int sig)
{
    return kill(getpid(), sig);
}
