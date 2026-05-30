; ─────────────────────────────────────────────────────────────
; userland/write.asm — ring-3 ELF: creates a file and writes to it.
;
; Usage:  exec /write.elf <filename>
; Example: exec /write.elf test.txt
;
; Syscalls: SYS_CREAT(7) → SYS_WRITE(1) → SYS_CLOSE(4) → SYS_EXIT(0)
; ─────────────────────────────────────────────────────────────

section .text
[BITS 32]
global _start

%define SYS_EXIT  0
%define SYS_WRITE 1
%define SYS_READ  2
%define SYS_OPEN  3
%define SYS_CLOSE 4
%define SYS_CREAT 7

%macro WRITE 2          ; buf_label, ret_label → stdout
    mov  ebx, 1
    lea  esi, [%1]
    mov  edi, %1 %+ _len
    mov  eax, SYS_WRITE
    lea  edx, [%2]
    mov  ecx, esp
    sysenter
%2:
%endmacro

_start:
    mov  ebp, esp

    ; Pick filename from argv[1], or default to "newfile"
    mov  edi, [ebp]             ; argc
    cmp  edi, 2
    jb   .use_default
    mov  ebx, [ebp + 8]         ; argv[1]
    jmp  .do_creat
.use_default:
    lea  ebx, [default_name]
.do_creat:

    ; ── 1. sys_creat(name) → fd ─────────────────────────
    mov  eax, SYS_CREAT
    lea  edx, [.r_creat]
    mov  ecx, esp
    sysenter
.r_creat:
    cmp  eax, -1
    je   .err_creat

    mov  [fd], eax              ; save fd

    ; ── 2. sys_write(fd, msg, msg_len) ─────────────────
    mov  eax, SYS_WRITE
    mov  ebx, [fd]              ; fd
    lea  esi, [content]         ; buf
    mov  edi, content_len       ; len
    lea  edx, [.r_write]
    mov  ecx, esp
    sysenter
.r_write:
    cmp  eax, -1
    je   .err_write

    ; ── 3. sys_close(fd) ────────────────────────────────
    mov  eax, SYS_CLOSE
    mov  ebx, [fd]
    lea  edx, [.r_close]
    mov  ecx, esp
    sysenter
.r_close:

    WRITE msg_ok, .r_ok

    ; ── 4. Re-open and read back to verify ──────────────
    ; Re-get filename
    mov  edi, [ebp]             ; argc
    cmp  edi, 2
    jb   .def2
    mov  ebx, [ebp + 8]
    jmp  .do_open
.def2:
    lea  ebx, [default_name]
.do_open:

    mov  eax, SYS_OPEN
    lea  edx, [.r_open]
    mov  ecx, esp
    sysenter
.r_open:
    cmp  eax, -1
    je   .err_open

    mov  [fd], eax

    ; sys_read(fd, buf, 256)
    mov  eax, SYS_READ
    mov  ebx, [fd]
    lea  esi, [buf]
    mov  edi, 256
    lea  edx, [.r_read]
    mov  ecx, esp
    sysenter
.r_read:

    ; Print what we read back
    mov  ebx, 1
    lea  esi, [buf]
    mov  edi, eax
    mov  eax, SYS_WRITE
    lea  edx, [.r_show]
    mov  ecx, esp
    sysenter
.r_show:

    mov  eax, SYS_CLOSE
    mov  ebx, [fd]
    lea  edx, [.r_close2]
    mov  ecx, esp
    sysenter
.r_close2:

    xor  ebx, ebx
    mov  eax, SYS_EXIT
    lea  edx, [.done]
    mov  ecx, esp
    sysenter
.done: jmp $

.err_creat:
    WRITE msg_err_creat, .r_ec
    jmp  .die
.err_write:
    WRITE msg_err_write, .r_ew
    jmp  .die
.err_open:
    WRITE msg_err_open, .r_eo
.die:
    mov  ebx, 1
    mov  eax, SYS_EXIT
    lea  edx, [.d2]
    mov  ecx, esp
    sysenter
.d2: jmp $

section .data
default_name:      db "newfile", 0
content:           db "Hello from NoxFS write!", 10
                   db "This file was created by write.elf", 10
content_len        equ $ - content
msg_ok:            db "  file written and verified.", 10
msg_ok_len         equ $ - msg_ok
msg_err_creat:     db "  error: cannot create file", 10
msg_err_creat_len  equ $ - msg_err_creat
msg_err_write:     db "  error: write failed", 10
msg_err_write_len  equ $ - msg_err_write
msg_err_open:      db "  error: cannot re-open file", 10
msg_err_open_len   equ $ - msg_err_open

section .bss
fd:   resd 1
buf:  resb 256
