; ─────────────────────────────────────────────────────────────
; userland/hello.asm — ring 3 ELF, loaded from disk by NoxFS,
;                      parsed by elf_load, jumped to via user_enter.
;
; Syscall convention (sysenter):
;   EAX = syscall #, EBX = arg1, ESI = arg2,
;   EDX = return EIP, ECX = user ESP
;
; All label refs are position-independent (call/pop) — paranoia
; even though the linker knows the load address (0x400000).
; ─────────────────────────────────────────────────────────────

section .text
[BITS 32]

global _start

_start:
    call .here
.here:
    pop  ebp                                  ; ebp = runtime &.here

    ; sys_write(1, msg, msg_len)   — EBX=fd, ESI=buf, EDI=len
    mov  ebx, 1
    lea  esi, [ebp + msg - .here]
    mov  edi, msg_len
    mov  eax, 1
    lea  edx, [ebp + .ret1 - .here]
    mov  ecx, esp
    sysenter
.ret1:

    ; sys_exit(0)
    mov  eax, 0                               ; SYS_EXIT
    xor  ebx, ebx                             ; exit code
    lea  edx, [ebp + .ret2 - .here]
    mov  ecx, esp
    sysenter
.ret2:
    jmp  $                                    ; unreachable: kernel longjmps

msg:    db "  hello from a real ELF on disk!", 10, 0
msg_len equ $ - msg
