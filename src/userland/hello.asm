; ─────────────────────────────────────────────────────────────
; src/userland/hello.asm — a real ELF64 ring-3 user program.
; Linked at 0x40000000 (outside the kernel identity map); loaded by
; the kernel's ELF64 loader.  Uses the Noxis syscall ABI.
; ─────────────────────────────────────────────────────────────
[BITS 64]
global _start

section .text
_start:
    mov  rax, 1                 ; SYS_WRITE
    mov  rdi, 1                 ; fd = stdout
    lea  rsi, [rel msg]         ; buf
    mov  rdx, msg_len           ; len
    syscall

    mov  rax, 0                 ; SYS_EXIT
    xor  rdi, rdi               ; code 0
    syscall
.hang:
    jmp .hang

section .rodata
msg:     db "Hello from a real ELF64 user program!", 10
msg_len  equ $ - msg
