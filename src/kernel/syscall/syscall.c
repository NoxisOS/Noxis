/**
 * @file    kernel/syscall/syscall.c
 * @brief   x86-64 syscall/sysret setup + dispatcher.
 *
 * The entry stub (syscall_entry.asm) hands us a syscall_frame_t holding the
 * full user register state; we read args from it and write the return value
 * back to f->rax.  This lets fork() clone the caller's exact context.
 */
#include <kernel/syscall/syscall.h>
#include <common/types.h>
#include <common/status.h>
#include <drivers/serial.h>
#include <drivers/vga.h>

void serial_write_hex64(uint64_t v);
int32_t kbd_poll(void);

/* fork/exec/exit live in proc/ (need process + vmm internals). */
int64_t  sys_fork(syscall_frame_t* f);
int64_t  sys_exec(syscall_frame_t* f, const uint8_t* path, const uint8_t** argv);
void     sys_exit(int code);
uint64_t sys_getpid(void);
int64_t  sys_waitpid(int64_t pid, int* status);
int64_t  sys_readdir(uint64_t idx, uint8_t* namebuf);
int64_t  sys_procinfo(uint64_t idx, uint8_t* buf);
int64_t  sys_pipe(int* fd);
int64_t  sys_signal(int sig, uint64_t handler);
int64_t  sys_kill(int64_t pid, int sig);
void     sys_setfg(uint64_t pid);
int64_t  sys_chdir(const uint8_t* path);
int64_t  sys_brk(uint64_t addr);
int64_t  sys_tcgetattr(int fd, void* out);
int64_t  sys_tcsetattr(int fd, int when, const void* in);
void     sys_sleep(uint32_t ms);
int64_t  sys_mkdir(const uint8_t* path);
int64_t  sys_unlink(const uint8_t* path);
int64_t  sys_stat(const uint8_t* path, void* buf);
int64_t  sys_rename(const uint8_t* old_path, const uint8_t* new_path);
int64_t  sys_getcwd(uint8_t* buf, uint64_t size);
int64_t  sys_getdents(const uint8_t* path, uint8_t* buf, uint64_t len);
void     deliver_signals(syscall_frame_t* f);

/* file descriptors live in proc/fd.c. */
int64_t  sys_open(const uint8_t* path, int flags);
int64_t  sys_close(int fd);
int64_t  sys_dup(int fd);
int64_t  sys_dup2(int oldfd, int newfd);
int64_t  sys_lseek(int fd, int64_t off, int whence);
int64_t  sys_read(int fd, uint8_t* buf, uint64_t len);
int64_t  sys_write(int fd, const uint8_t* buf, uint64_t len);

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
    SYS_OPEN = 7, SYS_CLOSE = 8, SYS_LSEEK = 9, SYS_READDIR = 10,
    SYS_DUP = 11, SYS_DUP2 = 12, SYS_PIPE = 13,
    SYS_KILL = 14, SYS_SIGNAL = 15, SYS_PROCINFO = 16,
    SYS_SETFG = 17,
    SYS_CHDIR = 18, SYS_GETCWD = 19, SYS_GETDENTS = 20,
    SYS_BRK = 21,
    SYS_MKDIR = 22, SYS_UNLINK = 23, SYS_STAT = 24, SYS_RENAME = 25,
    SYS_TCGETATTR = 26, SYS_TCSETATTR = 27,
    SYS_SLEEP = 28,
};

static void do_syscall(syscall_frame_t* f) {
    switch (f->rax) {
    case SYS_EXIT:    sys_exit((int)f->rdi);                       return;  /* no return */
    case SYS_WRITE:   f->rax = (uint64_t)sys_write((int)f->rdi,
                                  (const uint8_t*)f->rsi, f->rdx);          return;
    case SYS_READ:    f->rax = (uint64_t)sys_read((int)f->rdi,
                                  (uint8_t*)f->rsi, f->rdx);                return;
    case SYS_FORK:    f->rax = (uint64_t)sys_fork(f);                       return;
    case SYS_EXEC:    f->rax = (uint64_t)sys_exec(f, (const uint8_t*)f->rdi,
                                                  (const uint8_t**)f->rsi);    return;
    case SYS_GETPID:  f->rax = sys_getpid();                               return;
    case SYS_WAITPID: f->rax = (uint64_t)sys_waitpid((int64_t)f->rdi,
                                                     (int*)f->rsi);         return;
    case SYS_OPEN:    f->rax = (uint64_t)sys_open((const uint8_t*)f->rdi,
                                                  (int)f->rsi);            return;
    case SYS_CLOSE:   f->rax = (uint64_t)sys_close((int)f->rdi);            return;
    case SYS_LSEEK:   f->rax = (uint64_t)sys_lseek((int)f->rdi,
                                  (int64_t)f->rsi, (int)f->rdx);            return;
    case SYS_READDIR: f->rax = (uint64_t)sys_readdir(f->rdi,
                                  (uint8_t*)f->rsi);                       return;
    case SYS_DUP:     f->rax = (uint64_t)sys_dup((int)f->rdi);              return;
    case SYS_DUP2:    f->rax = (uint64_t)sys_dup2((int)f->rdi, (int)f->rsi); return;
    case SYS_PIPE:    f->rax = (uint64_t)sys_pipe((int*)f->rdi);            return;
    case SYS_KILL:    f->rax = (uint64_t)sys_kill((int64_t)f->rdi, (int)f->rsi); return;
    case SYS_SIGNAL:  f->rax = (uint64_t)sys_signal((int)f->rdi, f->rsi);   return;
    case SYS_PROCINFO:f->rax = (uint64_t)sys_procinfo(f->rdi, (uint8_t*)f->rsi); return;
    case SYS_SETFG:   sys_setfg(f->rdi); f->rax = 0;                        return;
    case SYS_CHDIR:   f->rax = (uint64_t)sys_chdir((const uint8_t*)f->rdi); return;
    case SYS_GETCWD:  f->rax = (uint64_t)sys_getcwd((uint8_t*)f->rdi, f->rsi); return;
    case SYS_GETDENTS:f->rax = (uint64_t)sys_getdents((const uint8_t*)f->rdi,
                                  (uint8_t*)f->rsi, f->rdx);               return;
    case SYS_BRK:     f->rax = (uint64_t)sys_brk(f->rdi);                  return;
    case SYS_MKDIR:   f->rax = (uint64_t)sys_mkdir((const uint8_t*)f->rdi); return;
    case SYS_UNLINK:  f->rax = (uint64_t)sys_unlink((const uint8_t*)f->rdi); return;
    case SYS_STAT:    f->rax = (uint64_t)sys_stat((const uint8_t*)f->rdi,
                                  (void*)f->rsi);                            return;
    case SYS_RENAME:    f->rax = (uint64_t)sys_rename((const uint8_t*)f->rdi,
                                    (const uint8_t*)f->rsi);                return;
    case SYS_TCGETATTR: f->rax = (uint64_t)sys_tcgetattr((int)f->rdi,
                                    (void*)f->rsi);                         return;
    case SYS_TCSETATTR: f->rax = (uint64_t)sys_tcsetattr((int)f->rdi,
                                    (int)f->rdx, (const void*)f->rsi);      return;
    case SYS_SLEEP:     sys_sleep((uint32_t)f->rdi); f->rax = 0;            return;
    default:          f->rax = (uint64_t)-1;                               return;
    }
}

void syscall_dispatch(syscall_frame_t* f) {
    do_syscall(f);
    deliver_signals(f);                /* deliver pending signals on return */
}
