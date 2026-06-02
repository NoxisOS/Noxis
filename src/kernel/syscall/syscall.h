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
#define SYS_SIGACTION 10 /* EBX=signum, ESI=act, EDI=oldact → EAX=0            */
#define SYS_KILL      11 /* EBX=pid, ESI=sig → EAX=0                           */
#define SYS_GETPID    12 /* → EAX=pid                                          */
#define SYS_IOCTL     13 /* EBX=fd, ESI=req, EDI=arg → EAX=0/-1                 */
#define SYS_MKDIR     14 /* EBX=path → EAX=0/-1                                   */
#define SYS_CHDIR     15 /* EBX=path → EAX=0/-1                                   */
#define SYS_GETDENTS  16 /* EBX=fd, ESI=buf, EDI=len, EDX=off → EAX=read          */
#define SYS_STAT      17 /* EBX=path, ESI=statbuf → EAX=0/-1                      */
#define SYS_LSEEK     18 /* EBX=fd, ESI=offset, EDI=whence → EAX=pos              */
#define SYS_EXECVE    19 /* EBX=path → replaces image, does not return (or -1)    */
#define SYS_BRK       20 /* EBX=new break (0=query) → EAX=current break           */
#define SYS_GETPPID   21 /* → EAX=parent pid                                      */
#define SYS_GETUID    22 /* → EAX=0 (single-user)                                 */
#define SYS_TIME      23 /* EBX=time_t* (opt) → EAX=seconds since boot            */
#define SYS_DUP2        24 /* EBX=oldfd, ESI=newfd → EAX=newfd                        */
#define SYS_SLEEP       25 /* EBX=ms → sleeps, EAX=0                                  */
#define SYS_SIGRETURN   26 /* restores CPU from signal frame; called by restorer tramp */
#define SYS_SIGPROCMASK 27 /* EBX=how, ESI=*new_set, EDI=*old_set → EAX=0/-1          */
#define SYS_UNLINK      28 /* EBX=path → EAX=0/-1                                   */
#define SYS_RENAME      29 /* EBX=old, ESI=new → EAX=0/-1                           */
#define SYS_CHMOD       30 /* EBX=path, ESI=mode → EAX=0/-1                         */
#define SYS_RMDIR       31 /* EBX=path → EAX=0/-1                                   */

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
