; ─────────────────────────────────────────────────────────────
; userland/fread.asm — ring-3 ELF: reads a file via sys_open /
; sys_read / sys_close and prints it to VGA.
;
; Usage:  exec /fread.elf <filename>
; Example: exec /fread.elf motd
;
; Syscalls used: SYS_OPEN(3)  SYS_READ(2)  SYS_WRITE(1)  SYS_CLOSE(4)
; ─────────────────────────────────────────────────────────────

section .text
[BITS 32]
global _start

%define SYS_EXIT  0
%define SYS_WRITE 1
%define SYS_READ  2
%define SYS_OPEN  3
%define SYS_CLOSE 4

%macro WRITE 2          ; buf_label, ret_label
    lea  ebx, [%1]
    mov  esi, %1 %+ _len
    mov  eax, SYS_WRITE
    lea  edx, [%2]
    mov  ecx, esp
    sysenter
%2:
%endmacro

_start:
    mov  ebp, esp

    ; Pick filename from argv[1], or default to "motd"
    mov  edi, [ebp]             ; argc
    cmp  edi, 2
    jb   .use_default
    mov  ebx, [ebp + 8]         ; argv[1]
    jmp  .do_open
.use_default:
    lea  ebx, [default_name]
.do_open:

    ; sys_open(name) → fd
    mov  eax, SYS_OPEN
    lea  edx, [.r_open]
    mov  ecx, esp
    sysenter
.r_open:
    cmp  eax, -1                ; -1 = error
    je   .err_open

    mov  [fd], eax              ; save fd

    ; Read loop: sys_read(fd, buf, 256) → bytes
.read_loop:
    mov  eax, SYS_READ
    mov  ebx, [fd]
    lea  esi, [buf]
    mov  edi, 256
    lea  edx, [.r_read]
    mov  ecx, esp
    sysenter
.r_read:
    cmp  eax, -1                ; -1 = error
    je   .err_read

    test eax, eax               ; 0 = EOF
    jz   .close

    ; sys_write(stdout, buf, bytes)
    mov  esi, eax               ; bytes read → length
    lea  ebx, [buf]
    mov  eax, SYS_WRITE
    lea  edx, [.r_write]
    mov  ecx, esp
    sysenter
.r_write:
    jmp  .read_loop

.close:
    ; sys_close(fd)
    mov  eax, SYS_CLOSE
    mov  ebx, [fd]
    lea  edx, [.r_close]
    mov  ecx, esp
    sysenter
.r_close:

    WRITE nl, .r_nl

    xor  ebx, ebx
    mov  eax, SYS_EXIT
    lea  edx, [.done]
    mov  ecx, esp
    sysenter
.done: jmp $

.err_open:
    WRITE msg_err_open, .r_eo
    jmp  .die

.err_read:
    WRITE msg_err_read, .r_er
    ; close the fd we already opened
    mov  eax, SYS_CLOSE
    mov  ebx, [fd]
    lea  edx, [.r_ec]
    mov  ecx, esp
    sysenter
.r_ec:

.die:
    mov  ebx, 1
    mov  eax, SYS_EXIT
    lea  edx, [.d2]
    mov  ecx, esp
    sysenter
.d2: jmp $

section .data
default_name:    db "motd", 0
msg_err_open:    db "  error: cannot open file", 10
msg_err_open_len equ $ - msg_err_open
msg_err_read:    db "  error: read failed", 10
msg_err_read_len equ $ - msg_err_read
nl:              db 10
nl_len           equ $ - nl

section .bss
fd:   resd 1
buf:  resb 256
