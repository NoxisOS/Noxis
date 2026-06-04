/**
 * @file    src/lib/noxlib/noxlib.h
 * @brief   Minimal 64-bit userland C runtime (syscall wrappers + helpers).
 */
#ifndef NOXLIB_H
#define NOXLIB_H

typedef unsigned long  size_t;
typedef long           ssize_t;

#define SYS_EXIT     0
#define SYS_WRITE    1
#define SYS_READ     2
#define SYS_FORK     3
#define SYS_EXEC     4
#define SYS_GETPID   5
#define SYS_WAITPID  6
#define SYS_OPEN     7
#define SYS_CLOSE    8
#define SYS_LSEEK    9
#define SYS_READDIR  10
#define SYS_DUP      11
#define SYS_DUP2     12
#define SYS_PIPE     13
#define SYS_KILL     14
#define SYS_SIGNAL   15
#define SYS_PROCINFO 16
#define SYS_SETFG    17
#define SYS_CHDIR    18
#define SYS_GETCWD   19
#define SYS_GETDENTS 20

#define SIGINT   2
#define SIGKILL  9
#define SIGUSR1  10
#define SIGTERM  15

#define O_RDONLY  0x00
#define O_WRONLY  0x01
#define O_RDWR    0x02
#define O_CREAT   0x40
#define O_TRUNC   0x200

#define SEEK_SET  0
#define SEEK_CUR  1
#define SEEK_END  2

/* ── raw syscalls (System V: rdi,rsi,rdx; num in rax; clobbers rcx,r11) ── */
static inline long _syscall3(long n, long a1, long a2, long a3) {
    long ret;
    __asm__ __volatile__("syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3)
        : "rcx", "r11", "memory");
    return ret;
}

static inline ssize_t write(int fd, const void* buf, size_t len) {
    return _syscall3(SYS_WRITE, fd, (long)buf, (long)len);
}
static inline ssize_t read(int fd, void* buf, size_t len) {
    return _syscall3(SYS_READ, fd, (long)buf, (long)len);
}
static inline void exit(int code) {
    _syscall3(SYS_EXIT, code, 0, 0);
    for (;;) { }
}

static inline long fork(void)            { return _syscall3(SYS_FORK, 0, 0, 0); }
static inline long execv(const char* p, char* const argv[]) {
    return _syscall3(SYS_EXEC, (long)p, (long)argv, 0);
}
static inline long getpid(void)          { return _syscall3(SYS_GETPID, 0, 0, 0); }
static inline long waitpid(long pid, int* st) {
    return _syscall3(SYS_WAITPID, pid, (long)st, 0);
}
static inline long open(const char* path, int flags) {
    return _syscall3(SYS_OPEN, (long)path, flags, 0);
}
static inline long close(int fd)         { return _syscall3(SYS_CLOSE, fd, 0, 0); }
static inline long lseek(int fd, long off, int whence) {
    return _syscall3(SYS_LSEEK, fd, off, whence);
}
static inline long readdir(long idx, char* namebuf) {
    return _syscall3(SYS_READDIR, idx, (long)namebuf, 0);
}
static inline long dup(int fd)            { return _syscall3(SYS_DUP, fd, 0, 0); }
static inline long dup2(int o, int n)     { return _syscall3(SYS_DUP2, o, n, 0); }
static inline long pipe(int fds[2])       { return _syscall3(SYS_PIPE, (long)fds, 0, 0); }
static inline long kill(long pid, int sig){ return _syscall3(SYS_KILL, pid, sig, 0); }
static inline long signal(int sig, void (*h)(int)) {
    return _syscall3(SYS_SIGNAL, sig, (long)h, 0);
}
typedef struct { long pid; long state; char name[32]; } procinfo_t;
static inline long procinfo(long idx, procinfo_t* pi) {
    return _syscall3(SYS_PROCINFO, idx, (long)pi, 0);
}
/* setfg(pid): register pid as the foreground process (Ctrl-C target).
   Pass 0 to clear (no foreground process). */
static inline void setfg(long pid) { _syscall3(SYS_SETFG, pid, 0, 0); }

static inline long chdir(const char* path) {
    return _syscall3(SYS_CHDIR, (long)path, 0, 0);
}
static inline long getcwd(char* buf, long size) {
    return _syscall3(SYS_GETCWD, (long)buf, size, 0);
}

/* Directory entry layout — mirrors noxfs_dirent_t (32 bytes). */
typedef struct {
    unsigned int   inode;
    unsigned short rec_len;
    unsigned char  name_len;
    unsigned char  type;    /* 1 = file, 2 = directory */
    char           name[24];
} dirent_t;

/* getdents(path, buf, len): fill buf with dirent_t entries from the
   directory at path (relative to cwd if not absolute).
   Returns bytes written, 0 at end, -1 on error. */
static inline long getdents(const char* path, void* buf, long len) {
    return _syscall3(SYS_GETDENTS, (long)path, (long)buf, len);
}

static inline size_t strlen(const char* s) {
    size_t n = 0; while (s[n]) n++; return n;
}
static inline int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
static inline void puts(const char* s) {
    write(1, s, strlen(s));
}

/* Print a signed decimal integer. */
static inline void puti(long v) {
    char buf[24];
    int i = sizeof(buf);
    unsigned long u = (v < 0) ? (unsigned long)(-v) : (unsigned long)v;
    buf[--i] = 0;
    do { buf[--i] = (char)('0' + (u % 10)); u /= 10; } while (u);
    if (v < 0) buf[--i] = '-';
    write(1, &buf[i], strlen(&buf[i]));
}

#endif /* NOXLIB_H */
