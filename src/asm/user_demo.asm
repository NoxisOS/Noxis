; ─────────────────────────────────────────────────────────────
; asm/user_demo.asm — User-mode demo (ring 3) — sysenter version
;
; The code is linked into the kernel ELF but copied to 0x400000
; at runtime, so all references to labels must be position-
; independent (otherwise they bake in kernel-virtual addresses
; that ring 3 cannot access).
;
; Convention: EAX=syscall#, EBX=arg1, ESI=arg2,
;             EDX=return EIP, ECX=user ESP.
; ─────────────────────────────────────────────────────────────

section .text
[BITS 32]

global _user_demo_start
global _user_demo_end

_user_demo_start:
    ; Get runtime load base via call/pop so all labels can be
    ; addressed as [ebp + (label - .here)] — an assembly-time const.
    call .here
.here:
    pop  ebp                                  ; ebp = runtime addr of .here

    ; ── sys_write(1, msg, len) ──────────────────────────────────
    lea  ebx, [ebp + _user_msg - .here]       ; ebx = msg
    mov  eax, 1                               ; SYS_WRITE
    mov  esi, _user_msg_len                   ; len
    lea  edx, [ebp + .ret1 - .here]           ; return EIP
    mov  ecx, esp                             ; user ESP
    sysenter
.ret1:

    ; ── sys_exit ────────────────────────────────────────────────
    mov  eax, 0                               ; SYS_EXIT
    lea  edx, [ebp + .ret2 - .here]
    mov  ecx, esp
    sysenter
.ret2:
    jmp  $

_user_msg:
    db "Hello via sysenter!", 10, 0
_user_msg_len equ $ - _user_msg

_user_demo_end:
