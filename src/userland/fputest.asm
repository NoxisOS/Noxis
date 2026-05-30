; ─────────────────────────────────────────────────────────────
; userland/fputest.asm — exercise the x87 FPU + lazy #NM switching.
;
;   (7 + 6) * 7 = 91, computed entirely on the x87 stack.
;   The first FPU instruction traps #NM (CR0.TS armed); the kernel
;   sets up this process's FPU state and re-runs the instruction.
;
; Absolute addressing (elf_load maps at link address 0x400000).
; ─────────────────────────────────────────────────────────────

section .text
[BITS 32]
global _start

%define SYS_EXIT   0
%define SYS_WRITE  1

%macro SYS 0
    lea  edx, [%%ret]
    mov  ecx, esp
    sysenter
%%ret:
%endmacro

_start:
    mov  dword [a], 7
    mov  dword [b], 6

    fild dword [a]        ; st0 = 7      (first FPU op → traps #NM)
    fild dword [b]        ; st0 = 6, st1 = 7
    faddp                 ; st0 = 13
    fimul dword [a]       ; st0 = 13 * 7 = 91
    fistp dword [r]       ; r = 91

    cmp  dword [r], 91
    jne  .fail

    mov  ebx, 1
    mov  esi, ok
    mov  edi, ok_len
    mov  eax, SYS_WRITE
    SYS
    xor  ebx, ebx
    mov  eax, SYS_EXIT
    SYS
.h1: jmp .h1

.fail:
    mov  ebx, 1
    mov  esi, bad
    mov  edi, bad_len
    mov  eax, SYS_WRITE
    SYS
    mov  ebx, 1
    mov  eax, SYS_EXIT
    SYS
.h2: jmp .h2

section .data
ok:     db "  fputest: x87 (7+6)*7 = 91 - OK", 10
ok_len  equ $ - ok
bad:    db "  fputest: FAILED", 10
bad_len equ $ - bad

section .bss
a: resd 1
b: resd 1
r: resd 1
