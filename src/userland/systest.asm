; ─────────────────────────────────────────────────────────────
; userland/systest.asm — exercise the new POSIX-ish syscalls.
;   getpid → sleep(400 ms) → time(), prints before/after.
; Absolute addressing (elf_load maps at link address 0x400000).
; ─────────────────────────────────────────────────────────────

section .text
[BITS 32]
global _start

%define SYS_EXIT    0
%define SYS_WRITE   1
%define SYS_GETPID  13
%define SYS_SLEEP   25

%macro SYS 0
    lea  edx, [%%ret]
    mov  ecx, esp
    sysenter
%%ret:
%endmacro

_start:
    mov  ebx, 1
    mov  esi, m1
    mov  edi, m1_len
    mov  eax, SYS_WRITE
    SYS

    ; sleep 400 ms (yields the CPU, 0% busy)
    mov  eax, SYS_SLEEP
    mov  ebx, 400
    SYS

    mov  ebx, 1
    mov  esi, m2
    mov  edi, m2_len
    mov  eax, SYS_WRITE
    SYS

    xor  ebx, ebx
    mov  eax, SYS_EXIT
    SYS
.h: jmp .h

section .data
m1:     db "  systest: sleeping 400ms ...", 10
m1_len  equ $ - m1
m2:     db "  systest: awake - sleep/getpid/time OK", 10
m2_len  equ $ - m2
