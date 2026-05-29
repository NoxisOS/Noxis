; ─────────────────────────────────────────────────────────────
; asm/user_demo.asm — User-mode demo (ring 3)
;
; Simple loop in ring 3. Proves user mode works without syscalls.
; Must be position-independent — copied to 0x400000 at runtime.
; ─────────────────────────────────────────────────────────────

section .text
[BITS 32]

global _user_demo_start
global _user_demo_end

_user_demo_start:
    jmp  $

_user_demo_end:
