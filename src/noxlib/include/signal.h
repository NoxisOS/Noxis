/**
 * @file    noxlib/include/signal.h
 * @brief   POSIX signal API for user-space programs
 * @author  Noxis Team
 * @date    2026-05-31
 */
#ifndef _NOXLIB_SIGNAL_H
#define _NOXLIB_SIGNAL_H

#include <stdint.h>
#include <sys/types.h>

/* ── Signal numbers ─────────────────────────────────────────── */
#define NSIG     32

#define SIGHUP    1
#define SIGINT    2
#define SIGQUIT   3
#define SIGILL    4
#define SIGTRAP   5
#define SIGABRT   6
#define SIGBUS    7
#define SIGFPE    8
#define SIGKILL   9
#define SIGUSR1   10
#define SIGSEGV   11
#define SIGUSR2   12
#define SIGPIPE   13
#define SIGALRM   14
#define SIGTERM   15
#define SIGCHLD   17
#define SIGCONT   18
#define SIGSTOP   19

/* ── Special handler values ─────────────────────────────────── */
typedef void (*sighandler_t)(int);

#define SIG_DFL  ((sighandler_t)0)
#define SIG_IGN  ((sighandler_t)1)
#define SIG_ERR  ((sighandler_t)-1)

/* ── sigprocmask how values ─────────────────────────────────── */
#define SIG_BLOCK    0
#define SIG_UNBLOCK  1
#define SIG_SETMASK  2

/* ── sa_flags ───────────────────────────────────────────────── */
#define SA_RESTORER  0x04000000u  /* sa_restorer field is valid */
#define SA_RESTART   0x10000000u  /* restart syscall after handler (TODO) */

/* ── sigset_t ───────────────────────────────────────────────── */
typedef uint32_t sigset_t;

/* ── struct sigaction ───────────────────────────────────────── */
/*
 * Layout MUST match the kernel's sigaction_t in common/signal.h.
 * The kernel reads: handler (4), flags (4), restorer (4) = 12 bytes.
 */
struct sigaction {
    sighandler_t  sa_handler;
    uint32_t      sa_flags;
    void        (*sa_restorer)(void);  /* set SA_RESTORER flag when using */
    sigset_t      sa_mask;             /* signals blocked during handler  */
};

/* ── Default restorer trampoline (defined in syscall.asm) ────── */
extern void __sig_restorer(void);

/* ── API ─────────────────────────────────────────────────────── */

/**
 * @brief Install a signal handler (simplified POSIX signal()).
 *        Automatically sets SA_RESTORER so the handler can return.
 *        Include <unistd.h> too for kill(), sigaction(), sigprocmask().
 * @return previous handler, or SIG_ERR on error.
 */
sighandler_t signal(int sig, sighandler_t handler);

/**
 * @brief Send signal sig to the calling process.
 */
int raise(int sig);

/* ── sigset_t helpers ───────────────────────────────────────── */
static inline void sigemptyset(sigset_t *s)           { *s = 0; }
static inline void sigfillset (sigset_t *s)           { *s = ~0u; }
static inline void sigaddset  (sigset_t *s, int sig)  { *s |=  (1u << sig); }
static inline void sigdelset  (sigset_t *s, int sig)  { *s &= ~(1u << sig); }
static inline int  sigismember(const sigset_t *s, int sig)
                                                      { return (*s >> sig) & 1; }

#endif /* _NOXLIB_SIGNAL_H */
