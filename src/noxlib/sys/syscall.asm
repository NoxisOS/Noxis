; ─────────────────────────────────────────────────────────────
; noxlib/sys/syscall.asm — sysenter wrappers for all Noxis syscalls
;
; Calling convention: i686 cdecl
;   Stack at function entry:
;     [esp+0]  = return address
;     [esp+4]  = arg1
;     [esp+8]  = arg2
;     [esp+12] = arg3
;   Return value: EAX
;   Callee-saved: EBX, ESI, EDI, EBP — we save/restore around sysenter.
;
; Sysenter convention (Noxis kernel):
;   EAX = syscall number
;   EBX = arg1
;   ESI = arg2
;   EDI = arg3
;   EDX = return EIP  (label of instruction immediately after sysenter)
;   ECX = user ESP    (restored by sysexit — stack is preserved)
;
; After sysexit:
;   EAX  = syscall return value (written by kernel into frame->eax)
;   EBX/ESI/EDI = values they had at sysenter time (= our args, not saved regs)
;   ESP  = ECX = stack as it was when we issued sysenter
;
; Therefore: push callee-saved regs BEFORE loading args, restore AFTER .ret.
; ─────────────────────────────────────────────────────────────

section .text
[BITS 32]

; ── Syscall numbers ─────────────────────────────────────────
%define SYS_EXIT      0
%define SYS_WRITE     1
%define SYS_READ      2
%define SYS_OPEN      3
%define SYS_CLOSE     4
%define SYS_FORK      5
%define SYS_WAITPID   6
%define SYS_CREAT     7
%define SYS_PIPE      8
%define SYS_DUP       9
%define SYS_SIGACTION 10
%define SYS_KILL      11
%define SYS_GETPID    12
%define SYS_IOCTL     13
%define SYS_MKDIR     14
%define SYS_CHDIR     15
%define SYS_GETDENTS  16
%define SYS_STAT      17
%define SYS_LSEEK     18
%define SYS_EXECVE    19
%define SYS_BRK       20
%define SYS_GETPPID   21
%define SYS_GETUID    22
%define SYS_TIME      23
%define SYS_DUP2        24
%define SYS_SLEEP       25
%define SYS_SIGRETURN   26
%define SYS_SIGPROCMASK 27

; ────────────────────────────────────────────────────────────
; 0-argument syscalls
; Stack layout: [esp+0]=retaddr  (push ebx → retaddr shifts to +4)
; ────────────────────────────────────────────────────────────

; pid_t fork(void)
global fork
fork:
    push ebx
    mov  eax, SYS_FORK
    lea  edx, [.ret]
    mov  ecx, esp
    sysenter
.ret:
    pop  ebx
    ret

; pid_t getpid(void)
global getpid
getpid:
    push ebx
    mov  eax, SYS_GETPID
    lea  edx, [.ret]
    mov  ecx, esp
    sysenter
.ret:
    pop  ebx
    ret

; pid_t getppid(void)
global getppid
getppid:
    push ebx
    mov  eax, SYS_GETPPID
    lea  edx, [.ret]
    mov  ecx, esp
    sysenter
.ret:
    pop  ebx
    ret

; uid_t getuid(void)
global getuid
getuid:
    push ebx
    mov  eax, SYS_GETUID
    lea  edx, [.ret]
    mov  ecx, esp
    sysenter
.ret:
    pop  ebx
    ret

; ────────────────────────────────────────────────────────────
; 1-argument syscalls
; Stack after push ebx:  [esp+0]=saved_ebx  [esp+4]=retaddr  [esp+8]=arg1
; ────────────────────────────────────────────────────────────

; void _exit(int status)   [noreturn — kernel never sysexit's]
global _exit
_exit:
    push ebx
    mov  eax, SYS_EXIT
    mov  ebx, [esp+8]        ; status
    lea  edx, [.ret]
    mov  ecx, esp
    sysenter
.ret:
    pop  ebx
    ret                      ; unreachable

; int close(int fd)
global close
close:
    push ebx
    mov  eax, SYS_CLOSE
    mov  ebx, [esp+8]        ; fd
    lea  edx, [.ret]
    mov  ecx, esp
    sysenter
.ret:
    pop  ebx
    ret

; int dup(int fd)
global dup
dup:
    push ebx
    mov  eax, SYS_DUP
    mov  ebx, [esp+8]        ; fd
    lea  edx, [.ret]
    mov  ecx, esp
    sysenter
.ret:
    pop  ebx
    ret

; int open(const char *path, int flags, ...)  — kernel only uses path
global open
open:
    push ebx
    mov  eax, SYS_OPEN
    mov  ebx, [esp+8]        ; path
    lea  edx, [.ret]
    mov  ecx, esp
    sysenter
.ret:
    pop  ebx
    ret

; int creat(const char *path)
global creat
creat:
    push ebx
    mov  eax, SYS_CREAT
    mov  ebx, [esp+8]        ; path
    lea  edx, [.ret]
    mov  ecx, esp
    sysenter
.ret:
    pop  ebx
    ret

; int mkdir(const char *path)
global mkdir
mkdir:
    push ebx
    mov  eax, SYS_MKDIR
    mov  ebx, [esp+8]        ; path
    lea  edx, [.ret]
    mov  ecx, esp
    sysenter
.ret:
    pop  ebx
    ret

; int chdir(const char *path)
global chdir
chdir:
    push ebx
    mov  eax, SYS_CHDIR
    mov  ebx, [esp+8]        ; path
    lea  edx, [.ret]
    mov  ecx, esp
    sysenter
.ret:
    pop  ebx
    ret

; int execv(const char *path, char *const argv[])
; int execve(const char *path, char *const argv[], char *const envp[])
;   EBX = path, ESI = argv — kernel copies strings before teardown.
;   envp is silently ignored (Noxis has no environment).
global execv
global execve
execv:
execve:
    push ebx
    push esi
    push edi
    mov  eax, SYS_EXECVE
    mov  ebx, [esp+16]       ; path
    mov  esi, [esp+20]       ; argv (char**) — may be NULL
    xor  edi, edi
    lea  edx, [.ret]
    mov  ecx, esp
    sysenter
.ret:
    pop  edi
    pop  esi
    pop  ebx
    ret                      ; only reached on error (EAX = -1)

; int _sys_brk(unsigned int addr)
global _sys_brk
_sys_brk:
    push ebx
    mov  eax, SYS_BRK
    mov  ebx, [esp+8]        ; new break (0 = query)
    lea  edx, [.ret]
    mov  ecx, esp
    sysenter
.ret:
    pop  ebx
    ret

; int sleep(unsigned int ms)
global sleep
sleep:
    push ebx
    mov  eax, SYS_SLEEP
    mov  ebx, [esp+8]        ; ms
    lea  edx, [.ret]
    mov  ecx, esp
    sysenter
.ret:
    pop  ebx
    ret

; int pipe(int fds[2])  — EBX = pointer to int[2]
global pipe
pipe:
    push ebx
    mov  eax, SYS_PIPE
    mov  ebx, [esp+8]        ; fds
    lea  edx, [.ret]
    mov  ecx, esp
    sysenter
.ret:
    pop  ebx
    ret

; pid_t waitpid(pid_t pid, int *status, int options)
; EBX = pid, ESI = options (status pointer is currently unused by kernel)
global waitpid
waitpid:
    push ebx
    push esi
    mov  eax, SYS_WAITPID
    mov  ebx, [esp+12]       ; pid   (1st arg, after 2 pushes)
    mov  esi, [esp+20]       ; options (3rd arg)
    lea  edx, [.ret]
    mov  ecx, esp
    sysenter
.ret:
    pop  esi
    pop  ebx
    ret

; time_t time(time_t *t)
global time
time:
    push ebx
    mov  eax, SYS_TIME
    mov  ebx, [esp+8]        ; t (optional)
    lea  edx, [.ret]
    mov  ecx, esp
    sysenter
.ret:
    pop  ebx
    ret

; ────────────────────────────────────────────────────────────
; 2-argument syscalls
; After push ebx + push esi:
;   [esp+0]=saved_esi  [esp+4]=saved_ebx  [esp+8]=retaddr
;   [esp+12]=arg1  [esp+16]=arg2
; ────────────────────────────────────────────────────────────

; int dup2(int oldfd, int newfd)
global dup2
dup2:
    push ebx
    push esi
    mov  eax, SYS_DUP2
    mov  ebx, [esp+12]       ; oldfd
    mov  esi, [esp+16]       ; newfd
    lea  edx, [.ret]
    mov  ecx, esp
    sysenter
.ret:
    pop  esi
    pop  ebx
    ret

; int kill(pid_t pid, int sig)
global kill
kill:
    push ebx
    push esi
    mov  eax, SYS_KILL
    mov  ebx, [esp+12]       ; pid
    mov  esi, [esp+16]       ; sig
    lea  edx, [.ret]
    mov  ecx, esp
    sysenter
.ret:
    pop  esi
    pop  ebx
    ret

; int stat(const char *path, void *statbuf)
global stat
stat:
    push ebx
    push esi
    mov  eax, SYS_STAT
    mov  ebx, [esp+12]       ; path
    mov  esi, [esp+16]       ; statbuf
    lea  edx, [.ret]
    mov  ecx, esp
    sysenter
.ret:
    pop  esi
    pop  ebx
    ret

; ────────────────────────────────────────────────────────────
; 3-argument syscalls
; After push ebx + push esi + push edi:
;   [esp+0]=saved_edi  [esp+4]=saved_esi  [esp+8]=saved_ebx
;   [esp+12]=retaddr
;   [esp+16]=arg1  [esp+20]=arg2  [esp+24]=arg3
; ────────────────────────────────────────────────────────────

; ssize_t write(int fd, const void *buf, size_t count)
global write
write:
    push ebx
    push esi
    push edi
    mov  eax, SYS_WRITE
    mov  ebx, [esp+16]       ; fd
    mov  esi, [esp+20]       ; buf
    mov  edi, [esp+24]       ; count
    lea  edx, [.ret]
    mov  ecx, esp
    sysenter
.ret:
    pop  edi
    pop  esi
    pop  ebx
    ret

; ssize_t read(int fd, void *buf, size_t count)
global read
read:
    push ebx
    push esi
    push edi
    mov  eax, SYS_READ
    mov  ebx, [esp+16]       ; fd
    mov  esi, [esp+20]       ; buf
    mov  edi, [esp+24]       ; count
    lea  edx, [.ret]
    mov  ecx, esp
    sysenter
.ret:
    pop  edi
    pop  esi
    pop  ebx
    ret

; off_t lseek(int fd, off_t offset, int whence)
global lseek
lseek:
    push ebx
    push esi
    push edi
    mov  eax, SYS_LSEEK
    mov  ebx, [esp+16]       ; fd
    mov  esi, [esp+20]       ; offset
    mov  edi, [esp+24]       ; whence
    lea  edx, [.ret]
    mov  ecx, esp
    sysenter
.ret:
    pop  edi
    pop  esi
    pop  ebx
    ret

; int ioctl(int fd, int request, int arg)
global ioctl
ioctl:
    push ebx
    push esi
    push edi
    mov  eax, SYS_IOCTL
    mov  ebx, [esp+16]       ; fd
    mov  esi, [esp+20]       ; request
    mov  edi, [esp+24]       ; arg
    lea  edx, [.ret]
    mov  ecx, esp
    sysenter
.ret:
    pop  edi
    pop  esi
    pop  ebx
    ret

; int sigaction(int sig, void *act, void *oldact)
global sigaction
sigaction:
    push ebx
    push esi
    push edi
    mov  eax, SYS_SIGACTION
    mov  ebx, [esp+16]       ; sig
    mov  esi, [esp+20]       ; act
    mov  edi, [esp+24]       ; oldact
    lea  edx, [.ret]
    mov  ecx, esp
    sysenter
.ret:
    pop  edi
    pop  esi
    pop  ebx
    ret

; int getdents(int fd, void *buf, int len)
global getdents
getdents:
    push ebx
    push esi
    push edi
    mov  eax, SYS_GETDENTS
    mov  ebx, [esp+16]       ; fd
    mov  esi, [esp+20]       ; buf
    mov  edi, [esp+24]       ; len
    lea  edx, [.ret]
    mov  ecx, esp
    sysenter
.ret:
    pop  edi
    pop  esi
    pop  ebx
    ret

; ── Signal support ───────────────────────────────────────────

; __sig_restorer — default signal trampoline.
;
; The handler was called with:
;   [esp+0] = __sig_restorer  (this address, as return addr)
;   [esp+4] = sig number
;   [esp+8] = sig_ucontext_t  (saved CPU state, 40 bytes)
;
; After the handler `ret`, ESP points at the sig number slot.
; int $0x80 fires: frame->user_esp = current ESP = that slot.
; sys_sigreturn finds uc at frame->user_esp + 4 and restores
; the complete pre-signal CPU state via the iret path.
global __sig_restorer
__sig_restorer:
    mov  eax, SYS_SIGRETURN
    int  0x80
    ; never reached — kernel restores EIP/ESP directly via iret

; int sigprocmask(int how, const uint32_t* set, uint32_t* oldset)
global sigprocmask
sigprocmask:
    push ebx
    push esi
    push edi
    mov  eax, SYS_SIGPROCMASK
    mov  ebx, [esp+16]       ; how
    mov  esi, [esp+20]       ; new_set (may be NULL)
    mov  edi, [esp+24]       ; old_set (may be NULL)
    lea  edx, [.ret]
    mov  ecx, esp
    sysenter
.ret:
    pop  edi
    pop  esi
    pop  ebx
    ret
