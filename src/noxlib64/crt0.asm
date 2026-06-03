; ─────────────────────────────────────────────────────────────
; src/noxlib64/crt0.asm — 64-bit userland C runtime entry.
; Calls main(), then exits with its return value via SYS_EXIT.
; ─────────────────────────────────────────────────────────────
[BITS 64]
global _start
extern main

section .text
_start:
    xor  rbp, rbp
    call main            ; int main(void)
    mov  rdi, rax        ; exit code = main's return value
    mov  rax, 0          ; SYS_EXIT
    syscall
.hang:
    jmp .hang
