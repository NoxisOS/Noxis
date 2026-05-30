; ─────────────────────────────────────────────────────────────
; userland/segv.asm — fatal page-fault demo.
;
; Deliberately writes to an unmapped user address outside the stack
; region.  The kernel's #PF handler classifies this as a fatal fault,
; prints a "segfault" line, and terminates the process — the kernel
; keeps running and the shell prompt returns (no triple fault).
; ─────────────────────────────────────────────────────────────

section .text
[BITS 32]
global _start

%define SYS_EXIT   0
%define SYS_WRITE  1

_start:
    call .here
.here:
    pop  ebp

    ; Announce intent.
    mov  ebx, 1
    lea  esi, [ebp + msg - .here]
    mov  edi, msg_len
    mov  eax, SYS_WRITE
    lea  edx, [ebp + .r1 - .here]
    mov  ecx, esp
    sysenter
.r1:

    ; Write to an unmapped address (< 3 GB, not in the stack region).
    mov  eax, 0xDEAD0000
    mov  dword [eax], 0x1234       ; → fatal page fault, process killed

    ; Unreachable.
    mov  eax, SYS_EXIT
    xor  ebx, ebx
    lea  edx, [ebp + .r2 - .here]
    mov  ecx, esp
    sysenter
.r2:
    jmp  $

msg:    db "  segv: about to touch 0xDEAD0000 ...", 10, 0
msg_len equ $ - msg - 1
