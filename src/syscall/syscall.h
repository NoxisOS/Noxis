/**
 * @file    syscall/syscall.h
 * @brief   System call interface
 * @author  Noxis Team
 * @date    2026-05-30
 */
#ifndef SYSCALL_SYSCALL_H
#define SYSCALL_SYSCALL_H

#include <common/types.h>
#include <common/status.h>

/* ── syscall numbers ───────────────────────────────────────── */
#define SYS_EXIT     0
#define SYS_WRITE    1   /* fd=EBX: 1=stdout→VGA, 3+=file; buf=ESI, len=EDI  */
#define SYS_READ     2   /* EBX=fd, ESI=buf, EDI=max_len → EAX=read          */
#define SYS_OPEN     3   /* EBX=name → EAX=fd                                 */
#define SYS_CLOSE    4   /* EBX=fd  → EAX=0                                   */
#define SYS_FORK     5   /* → EAX=child_pid (parent) or 0 (child)             */
#define SYS_WAITPID  6   /* EBX=pid → EAX=exit_code                           */
#define SYS_CREAT    7   /* EBX=name → EAX=fd                                 */
#define SYS_PIPE     8   /* EBX=fd[2] (user ptr to 2 ints) → EAX=0             */
#define SYS_DUP      9   /* EBX=oldfd → EAX=newfd                              */

/* ── file descriptors ──────────────────────────────────────── */
#define STDIN_FD     0
#define STDOUT_FD    1
#define STDERR_FD    2

/**
 * @brief Initializes the syscall table and registers int 0x80 handler
 * @return OS_OK on success
 */
os_status_t syscall_init(void);

#endif /* SYSCALL_SYSCALL_H */
