/**
 * @file    src/noxlib64/noxlib.h
 * @brief   Minimal 64-bit userland C runtime (syscall wrappers + helpers).
 */
#ifndef NOXLIB64_H
#define NOXLIB64_H

typedef unsigned long  size_t;
typedef long           ssize_t;

#define SYS_EXIT   0
#define SYS_WRITE  1
#define SYS_READ   2

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

static inline size_t strlen(const char* s) {
    size_t n = 0; while (s[n]) n++; return n;
}
static inline void puts(const char* s) {
    write(1, s, strlen(s));
}

#endif /* NOXLIB64_H */
