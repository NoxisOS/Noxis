; ─────────────────────────────────────────────────────────────
; src/boot64/entry.asm — 64-bit kernel entry stub.
; Linked first (at the kernel load address); the boot sector jumps here
; once the CPU is in long mode.  Sets up a stack and calls kmain64().
; ─────────────────────────────────────────────────────────────
[BITS 64]

global _start64
extern kmain64

section .text
_start64:
    mov  rsp, 0x9F000          ; stack below 0x100000, inside the mapped 2 MB
    xor  rbp, rbp
    call kmain64
.hang:
    hlt
    jmp  .hang
