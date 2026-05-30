; ─────────────────────────────────────────────────────────────
; userland/prompt.asm — ask the user's name, greet them.
;
; Demonstrates: SYS_WRITE (output) + SYS_READ (line input).
;
; SYS_READ: EAX=2, EBX=fd(0=stdin), ESI=buf, EDI=maxlen → EAX=bytes read
; SYS_WRITE: EAX=1, EBX=fd(1=stdout), ESI=buf, EDI=len

section .text
[BITS 32]
global _start

%define SYS_EXIT  0
%define SYS_WRITE 1
%define SYS_READ  2
%define STDIN     0

%macro WRITE 2          ; buf_label, ret_label
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
    ; print prompt
    WRITE msg_ask, .r1

    ; sys_read(stdin, name_buf, 64)
    mov  eax, SYS_READ
    mov  ebx, STDIN
    lea  esi, [name_buf]
    mov  edi, 64
    lea  edx, [.r2]
    mov  ecx, esp
    sysenter
.r2:
    ; eax = bytes read (includes '\n')
    ; strip trailing '\n' — replace with '\0'
    test eax, eax
    jz   .skip_strip
    lea  ebx, [name_buf]
    add  ebx, eax
    dec  ebx                    ; point at last char
    cmp  byte [ebx], 10
    jne  .skip_strip
    mov  byte [ebx], 0
    dec  eax                    ; adjust len
.skip_strip:
    mov  [name_len], eax

    ; print "Hello, "
    WRITE msg_hello, .r3

    ; print name
    mov  ebx, 1
    lea  esi, [name_buf]
    mov  edi, [name_len]
    mov  eax, SYS_WRITE
    lea  edx, [.r4]
    mov  ecx, esp
    sysenter
.r4:

    ; print "!\n"
    WRITE msg_bang, .r5

    xor  ebx, ebx
    mov  eax, SYS_EXIT
    lea  edx, [.end]
    mov  ecx, esp
    sysenter
.end: jmp $

section .data
msg_ask:       db "  What is your name? "
msg_ask_len    equ $ - msg_ask
msg_hello:     db "  Hello, "
msg_hello_len  equ $ - msg_hello
msg_bang:      db "!", 10
msg_bang_len   equ $ - msg_bang

section .bss
name_buf:  resb 64
name_len:  resd 1
