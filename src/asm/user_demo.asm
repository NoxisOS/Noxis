; ─────────────────────────────────────────────────────────────
; asm/user_demo.asm — User-mode demo (ring 3) — calls sys_write
;
; Position-independent: copied to 0x400000 at runtime.
; Uses call/pop to find its own address for string references.
; ─────────────────────────────────────────────────────────────

section .text
[BITS 32]

global _user_demo_start
global _user_demo_end

_user_demo_start:
    ; Get current EIP into EBX via call/pop
    call .next
.next:
    pop  ebx                          ; ebx = address of .next

    ; Calculate message address relative to our position
    lea  ebx, [ebx + _user_msg - .next]

    ; sys_write(SYS_WRITE=1, str=ebx, len=ecx)
    mov  eax, 1                       ; SYS_WRITE
    mov  ecx, _user_msg_len
    int  0x80

    ; sys_exit
    mov  eax, 0                       ; SYS_EXIT
    int  0x80

    jmp  $

_user_msg:
    db "Hello from ring 3!", 10, 0
_user_msg_len equ $ - _user_msg

_user_demo_end:
