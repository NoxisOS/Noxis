/**
 * @file    kernel/syscall/syscall64.c
 * @brief   x86-64 syscall/sysret setup + dispatcher.
 *
 * The entry stub (syscall_entry.asm) hands us a syscall_frame_t holding the
 * full user register state; we read args from it and write the return value
 * back to f->rax.  This lets fork() clone the caller's exact context.
 */
#include <kernel/syscall/syscall64.h>
#include <common/types.h>
#include <common/status.h>
#include <drivers/serial.h>
#include <drivers/vga.h>

void serial_write_hex64(uint64_t v);
int32_t kbd_poll(void);

/* fork/exec/exit live in proc/ (need process + vmm internals). */
int64_t  sys_fork(syscall_frame_t* f);
int64_t  sys_exec(syscall_frame_t* f, const uint8_t* path);
void     sys_exit(int code);
uint64_t sys_getpid(void);
int64_t  sys_waitpid(int64_t pid, int* status);

/* ── MSRs ─────────────────────────────────────────────────────── */
#define MSR_EFER    0xC0000080
#define MSR_STAR    0xC0000081
#define MSR_LSTAR   0xC0000082
#define MSR_SFMASK  0xC0000084

extern void syscall_entry(void);   /* syscall_entry.asm */

uint64_t g_cur_kstack = 0;          /* top of the current proc's kernel stack */
uint64_t g_user_rsp   = 0;          /* scratch: user RSP across a syscall      */

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ __volatile__("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}
static inline void wrmsr(uint32_t msr, uint64_t v) {
    __asm__ __volatile__("wrmsr" :: "c"(msr), "a"((uint32_t)v), "d"((uint32_t)(v >> 32)));
}

os_status_t syscall_init(void) {
    /* STAR: [47:32] = syscall CS (0x08), [63:48] = sysret base (0x10).
       → syscall:  CS=0x08, SS=0x10
       → sysret:   CS=0x10+16=0x20|3, SS=0x10+8=0x18|3 */
    wrmsr(MSR_STAR, ((uint64_t)0x10 << 48) | ((uint64_t)0x08 << 32));
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
    wrmsr(MSR_SFMASK, 0x200);             /* clear IF on entry */
    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | 1); /* EFER.SCE = enable syscall */
    return OS_OK;
}

/* ── Syscall numbers ──────────────────────────────────────────── */
enum {
    SYS_EXIT = 0, SYS_WRITE = 1, SYS_READ = 2,
    SYS_FORK = 3, SYS_EXEC = 4, SYS_GETPID = 5, SYS_WAITPID = 6,
};

void syscall_dispatch(syscall_frame_t* f) {
    switch (f->rax) {
    case SYS_EXIT:
        sys_exit((int)f->rdi);
        return;                       /* never returns */

    case SYS_WRITE:
        vga_write_buf((const uint8_t*)f->rsi, (uint32_t)f->rdx);
        serial_write_n((const uint8_t*)f->rsi, (uint32_t)f->rdx);
        f->rax = f->rdx;
        return;

    case SYS_READ: {                  /* line-buffered keyboard read */
        uint8_t* buf = (uint8_t*)f->rsi;
        uint64_t max = f->rdx, got = 0;
        while (got < max) {
            int32_t c;
            __asm__ __volatile__("sti");   /* let the keyboard IRQ fire */
            while ((c = kbd_poll()) < 0) __asm__ __volatile__("hlt");
            if (c == '\r') c = '\n';
            buf[got++] = (uint8_t)c;
            vga_put_char((uint8_t)c);
            if (c == '\n') break;
        }
        __asm__ __volatile__("cli");
        f->rax = got;
        return;
    }

    case SYS_FORK:    f->rax = (uint64_t)sys_fork(f);                       return;
    case SYS_EXEC:    f->rax = (uint64_t)sys_exec(f, (const uint8_t*)f->rdi); return;
    case SYS_GETPID:  f->rax = sys_getpid();                               return;
    case SYS_WAITPID: f->rax = (uint64_t)sys_waitpid((int64_t)f->rdi,
                                                     (int*)f->rsi);         return;

    default:
        f->rax = (uint64_t)-1;
        return;
    }
}
