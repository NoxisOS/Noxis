; ─────────────────────────────────────────────────────────────
; userland/pftest.asm — demand-paging demo.
;
; Walks the stack pointer 64 KB downward, touching one byte per 4 KB
; page.  Each touch hits an unmapped page in the user-stack region, so
; the kernel's #PF handler maps a fresh zero-filled page on demand.
; If demand paging works, all 16 pages fault in and the program prints
; a success line; otherwise the process would be killed.
;
; Syscall convention (sysenter): EAX=#, EBX=arg1, ESI=arg2, EDI=arg3,
;                                EDX=return EIP, ECX=user ESP
; ─────────────────────────────────────────────────────────────

section .text
[BITS 32]
global _start

%define SYS_EXIT   0
%define SYS_WRITE  1

_start:
    call .here
.here:
    pop  ebp                       ; ebp = runtime &.here

    ; Touch 16 pages (64 KB) below the current stack pointer.
    mov  edx, esp                  ; edx = walking pointer
    mov  ecx, 16                   ; pages to touch
.loop:
    sub  edx, 0x1000               ; next page down
    mov  byte [edx], 0xAA          ; demand-fault this page into existence
    dec  ecx
    jnz  .loop

    ; sys_write(1, msg, msg_len)
    mov  ebx, 1
    lea  esi, [ebp + msg - .here]
    mov  edi, msg_len
    mov  eax, SYS_WRITE
    lea  edx, [ebp + .r1 - .here]
    mov  ecx, esp
    sysenter
.r1:

    ; sys_exit(0)
    mov  eax, SYS_EXIT
    xor  ebx, ebx
    lea  edx, [ebp + .r2 - .here]
    mov  ecx, esp
    sysenter
.r2:
    jmp  $

msg:    db "  pftest: touched 64 KB of stack via demand paging - OK", 10, 0
msg_len equ $ - msg - 1
