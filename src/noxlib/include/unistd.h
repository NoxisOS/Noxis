/**
 * @file    noxlib/include/unistd.h
 * @brief   POSIX syscall wrappers — maps 1:1 to Noxis syscall table
 */
#ifndef _NOXLIB_UNISTD_H
#define _NOXLIB_UNISTD_H

#include <stddef.h>
#include <sys/types.h>

/* ── Standard file descriptors ─────────────────────────── */
#define STDIN_FILENO   0
#define STDOUT_FILENO  1
#define STDERR_FILENO  2

/* ── lseek whence ───────────────────────────────────────── */
#define SEEK_SET  0
#define SEEK_CUR  1
#define SEEK_END  2

/* ── open flags (kernel only uses path for now) ─────────── */
#define O_RDONLY  0
#define O_WRONLY  1
#define O_RDWR    2
#define O_CREAT   0x40
#define O_TRUNC   0x200

/* ── File I/O ───────────────────────────────────────────── */
ssize_t read  (int fd, void *buf, size_t count);
ssize_t write (int fd, const void *buf, size_t count);
int     open  (const char *path, int flags, ...);
int     close (int fd);
int     creat (const char *path);
int     dup   (int fd);
int     dup2  (int oldfd, int newfd);
off_t   lseek (int fd, off_t offset, int whence);
int     ioctl (int fd, int request, ...);
int     stat  (const char *path, void *statbuf);
int     getdents(int fd, void *buf, int len);

/* ── Process ────────────────────────────────────────────── */
pid_t   fork    (void);
int     execv   (const char *path, char *const argv[]);
int     execve  (const char *path, char *const argv[], char *const envp[]);
pid_t   waitpid (pid_t pid, int *status, int options);
pid_t   getpid  (void);
pid_t   getppid (void);
uid_t   getuid  (void);

/* ── Signals ────────────────────────────────────────────── */
int     kill        (pid_t pid, int sig);
int     sigaction   (int sig, void *act, void *oldact);
int     sigprocmask (int how, const void *set, void *oldset);

/* ── waitpid options ────────────────────────────────────── */
#define WNOHANG  1

/* ── Filesystem ─────────────────────────────────────────── */
int     mkdir  (const char *path);
int     rmdir  (const char *path);
int     chdir  (const char *path);
int     pipe   (int pipefd[2]);
int     unlink (const char *path);
int     rename (const char *old, const char *newpath);
int     chmod  (const char *path, int mode);

/* ── permission bit constants ───────────────────────────────── */
#define S_IRUSR  0400
#define S_IWUSR  0200
#define S_IXUSR  0100
#define S_IRGRP  0040
#define S_IWGRP  0020
#define S_IXGRP  0010
#define S_IROTH  0004
#define S_IWOTH  0002
#define S_IXOTH  0001
#define S_IRWXU  (S_IRUSR|S_IWUSR|S_IXUSR)
#define S_IRWXG  (S_IRGRP|S_IWGRP|S_IXGRP)
#define S_IRWXO  (S_IROTH|S_IWOTH|S_IXOTH)

/* ── Misc ───────────────────────────────────────────────── */
int     sleep  (unsigned int ms);
time_t  time   (time_t *t);

/* Raw brk — use malloc/free instead of calling this directly. */
int     _sys_brk(unsigned int addr);

#endif /* _NOXLIB_UNISTD_H */
