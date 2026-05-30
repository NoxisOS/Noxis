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

typedef void (*sighandler_t)(int);

typedef struct {
    sighandler_t handler;
    uint32_t     flags;
} sigaction_t;

#endif /* COMMON_SIGNAL_H */
