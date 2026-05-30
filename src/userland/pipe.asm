; ─────────────────────────────────────────────────────────────
; userland/pipe.asm — ring-3 pipe() + fork() demo.
;
; Parent creates a pipe, forks.  Child writes a message to the
; pipe write-end, then exits.  Parent reads from the pipe and
; prints the message.
;
; Syscalls: SYS_PIPE(8)  SYS_FORK(5)  SYS_WRITE(1)  SYS_READ(2)
;           SYS_CLOSE(4)  SYS_WAITPID(6)  SYS_EXIT(0)
; ─────────────────────────────────────────────────────────────

section .text
[BITS 32]
global _start

%define SYS_EXIT    0
%define SYS_WRITE   1
%define SYS_READ    2
%define SYS_CLOSE   4
%define SYS_FORK    5
%define SYS_WAITPID 6
%define SYS_PIPE    8

%macro SYSWRITE 3          ; buf_label, len_expr, .ret  (fd=stdout)
    mov  ebx, 1
    lea  esi, [%1]
    mov  edi, %2
    mov  eax, SYS_WRITE
    lea  edx, [%3]
    mov  ecx, esp
    sysenter
%3:
%endmacro

%macro SYSEXIT 2           ; code, .ret
    mov  eax, SYS_EXIT
    mov  ebx, %1
    lea  edx, [%2]
    mov  ecx, esp
    sysenter
%2: jmp $
%endmacro

_start:
    ; ── 1. sys_pipe(fds) → EAX=0 ──────────────────────────
    lea  ebx, [fds]
    mov  eax, SYS_PIPE
    lea  edx, [.r_pipe]
    mov  ecx, esp
    sysenter
.r_pipe:
    cmp  eax, -1
    je   .err_pipe

    ; ── 2. sys_fork() ─────────────────────────────────────
    mov  eax, SYS_FORK
    lea  edx, [.r_fork]
    mov  ecx, esp
    sysenter
.r_fork:
    test eax, eax
    jz   .child

    ; ── PARENT ────────────────────────────────────────────
    ; EAX = child_pid from fork.  Save it on the stack — it will be
    ; clobbered by every subsequent syscall.
    push eax             ; [esp] = child_pid

    ; Close write-end (fd[1]) so pipe_read can detect EOF when child exits.
    mov  eax, SYS_CLOSE
    mov  ebx, [fds + 4]
    lea  edx, [.r_close_w]
    mov  ecx, esp
    sysenter
.r_close_w:

    ; Read from pipe (fd[0]) — blocks until child writes then closes its end.
    mov  eax, SYS_READ
    mov  ebx, [fds]
    lea  esi, [buf]
    mov  edi, 256
    lea  edx, [.r_read]
    mov  ecx, esp
    sysenter
.r_read:
    push eax             ; [esp] = bytes_read, [esp+4] = child_pid

    ; Print what we read (bytes_read bytes from buf).
    pop  edi             ; edi = bytes_read
    push edi             ; put it back so stack stays balanced for child_pid pop
    mov  ebx, 1
    lea  esi, [buf]
    ; edi already = bytes_read (arg3 for sys_write)
    mov  eax, SYS_WRITE
    lea  edx, [.r_show]
    mov  ecx, esp
    sysenter
.r_show:
    add  esp, 4          ; discard bytes_read

    ; Close read-end.
    mov  eax, SYS_CLOSE
    mov  ebx, [fds]
    lea  edx, [.r_close_r]
    mov  ecx, esp
    sysenter
.r_close_r:

    ; waitpid(child_pid) so the child is properly reaped.
    pop  ebx             ; ebx = child_pid
    mov  eax, SYS_WAITPID
    lea  edx, [.r_wait]
    mov  ecx, esp
    sysenter
.r_wait:

    SYSEXIT 0, .parent_done

    ; ── CHILD ─────────────────────────────────────────────
.child:
    ; Close read-end (fd[0])
    mov  eax, SYS_CLOSE
    mov  ebx, [fds]
    lea  edx, [.r_cclose_r]
    mov  ecx, esp
    sysenter
.r_cclose_r:

    ; Write to pipe (fd[1])
    mov  eax, SYS_WRITE
    mov  ebx, [fds + 4]      ; write-end fd
    lea  esi, [msg]
    mov  edi, msg_len
    lea  edx, [.r_cwrite]
    mov  ecx, esp
    sysenter
.r_cwrite:

    ; Close write-end
    mov  eax, SYS_CLOSE
    mov  ebx, [fds + 4]
    lea  edx, [.r_cclose_w]
    mov  ecx, esp
    sysenter
.r_cclose_w:

    SYSEXIT 42, .child_done

.err_pipe:
    SYSWRITE msg_err, msg_err_len, .r_err
    SYSEXIT 1, .die

section .data
msg:             db "Hello through a pipe!", 10
msg_len          equ $ - msg
msg_err:         db "  error: pipe() failed", 10
msg_err_len      equ $ - msg_err

section .bss
fds:  resd 2     ; fd[0] = read, fd[1] = write
buf:  resb 256
