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
    mov  rdi, [rsp]      ; argc  (kernel placed it at the top of the stack)
    lea  rsi, [rsp + 8]  ; argv  (array of pointers follows argc)
    call main            ; int main(int argc, char** argv)
    mov  rdi, rax        ; exit code = main's return value
    mov  rax, 0          ; SYS_EXIT
    syscall
.hang:
    jmp .hang
