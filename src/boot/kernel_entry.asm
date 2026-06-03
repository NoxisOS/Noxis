; ─────────────────────────────────────────────────────────────
; src/boot/kernel_entry.asm — 64-bit higher-half kernel entry.
;
; The boot sector jumps here at the kernel's PHYSICAL address (0x10000),
; which is identity-mapped low.  The kernel is LINKED at the higher-half
; virtual base, and the boot also mapped 0xFFFFFFFF80000000 → phys 0.
; So _start first jumps from low to its high virtual address, then runs
; the rest of the kernel from the higher half.
; ─────────────────────────────────────────────────────────────

section .text.entry
[BITS 64]

global _start
extern kernel_main
extern _bss_start
extern _bss_end

_start:
    ; Running at low physical (identity-mapped). Jump to the high alias.
    mov  rax, _start_high
    jmp  rax

_start_high:
    ; Now RIP is in the higher half. Zero BSS.
    lea  rdi, [rel _bss_start]
    lea  rcx, [rel _bss_end]
    sub  rcx, rdi
    shr  rcx, 3
    xor  rax, rax
    cld
    rep  stosq

    lea  rsp, [rel _kernel_stack_top]
    xor  rbp, rbp
    call kernel_main

.halt:
    cli
    hlt
    jmp .halt

section .bss
align 16
_kernel_stack_bottom:
    resb 32768
_kernel_stack_top:
