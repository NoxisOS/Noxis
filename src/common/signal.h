/**
 * @file    common/signal.h
 * @brief   POSIX signal numbers and structures
 * @author  Noxis Team
 * @date    2026-05-30
 */
#ifndef COMMON_SIGNAL_H
#define COMMON_SIGNAL_H

#include <common/types.h>

#define NSIG          32

#define SIGHUP        1
#define SIGINT        2
#define SIGQUIT       3
#define SIGILL        4
#define SIGTRAP       5
#define SIGABRT       6
#define SIGBUS        7
#define SIGFPE        8
#define SIGKILL       9
#define SIGUSR1       10
#define SIGSEGV       11
#define SIGUSR2       12
#define SIGPIPE       13
#define SIGALRM       14
#define SIGTERM       15
#define SIGCHLD       17
#define SIGCONT       18
#define SIGSTOP       19

#define SIG_DFL  ((void*)0)
#define SIG_IGN  ((void*)1)

/* sa_flags bit */
#define SA_RESTORER  0x04000000u

typedef void (*sighandler_t)(int);

/**
 * @brief Saved user CPU context pushed onto the signal stack frame.
 *        Restored by sys_sigreturn after the handler returns.
 */
typedef struct {
    uint32_t eax, ecx, edx, ebx, esi, edi, ebp;
    uint32_t eip;      /* interrupted EIP to resume at          */
    uint32_t eflags;   /* full EFLAGS (IF is forced back on)    */
    uint32_t esp;      /* user ESP before signal delivery       */
} sig_ucontext_t;

/**
 * @brief Per-signal action installed by sigaction().
 *        restorer MUST be set (SA_RESTORER flag required for custom handlers).
 */
typedef struct {
    sighandler_t  handler;
    uint32_t      flags;
    void        (*restorer)(void);   /* user-space trampoline → sys_sigreturn */
} sigaction_t;

#endif /* COMMON_SIGNAL_H */
