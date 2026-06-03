; ─────────────────────────────────────────────────────────────
; src/boot/kernel_entry.asm — 64-bit kernel entry.
;
; The long-mode boot sector (boot.asm) has already:
;   - entered long mode with the first 1 GB identity-mapped
;   - loaded this kernel flat at physical 0x100000
;   - far-jumped to 0x100000 (where _start sits, .text.entry first)
;
; Here we zero BSS, set up the kernel stack, and call kernel_main().
; ─────────────────────────────────────────────────────────────

section .text.entry
[BITS 64]

global _start
extern kernel_main
extern _bss_start
extern _bss_end

_start:
    ; ── Zero BSS ──────────────────────────────────────────────
    lea  rdi, [rel _bss_start]
    lea  rcx, [rel _bss_end]
    sub  rcx, rdi
    shr  rcx, 3                  ; qword count
    xor  rax, rax
    cld
    rep  stosq

    ; ── Kernel stack ──────────────────────────────────────────
    lea  rsp, [rel _kernel_stack_top]
    xor  rbp, rbp

    call kernel_main

.halt:
    cli
    hlt
    jmp  .halt

; ── Kernel stack (32 KB, in BSS) ────────────────────────────
section .bss
align 16
_kernel_stack_bottom:
    resb 32768
_kernel_stack_top:
