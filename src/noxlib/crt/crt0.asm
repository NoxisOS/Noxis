; ─────────────────────────────────────────────────────────────
; noxlib/crt/crt0.asm — C runtime entry point
;
; The kernel's elf_load jumps here (ring 3, EIP = 0x400000).
; We set up a clean stack frame, call main(0, NULL), then exit()
; with the return value.  Never returns.
;
; Calling convention: i686 cdecl — args pushed right-to-left.
; ─────────────────────────────────────────────────────────────

section .text
[BITS 32]

global _start
extern main
extern exit

_start:
    xor  ebp, ebp        ; mark outermost frame (no caller)
    push 0               ; argv = NULL
    push 0               ; argc = 0
    call main
    add  esp, 8          ; clean up argc/argv
    push eax             ; exit(main_return_value)
    call exit
    jmp  $               ; unreachable — exit() never returns
