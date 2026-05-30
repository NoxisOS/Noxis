; ─────────────────────────────────────────────────────────────
; userland/fork.asm — ring-3 fork() demo.
;
; Parent calls fork():
;   - child  (EAX==0): prints "child: hello from fork!\n", exits 42.
;   - parent (EAX!=0): waitpid(child_pid), prints exit code, exits 0.
;
; Syscall convention (sysenter):
;   EAX=#  EBX=arg1  ESI=arg2  EDI=arg3  EDX=retEIP  ECX=userESP
; ─────────────────────────────────────────────────────────────

section .text
[BITS 32]
global _start

%define SYS_EXIT    0
%define SYS_WRITE   1
%define SYS_FORK    5
%define SYS_WAITPID 6

; ── sysenter wrappers ────────────────────────────────────────

%macro SYSWRITE 3          ; SYSWRITE buf_label, len_expr, .ret
    mov  eax, SYS_WRITE
    lea  ebx, [%1]
    mov  esi, %2
    lea  edx, [%3]
    mov  ecx, esp
    sysenter
%3:
%endmacro

%macro SYSEXIT 2           ; SYSEXIT code_reg_or_imm, .ret
    mov  eax, SYS_EXIT
    mov  ebx, %1
    lea  edx, [%2]
    mov  ecx, esp
    sysenter
%2: jmp $
%endmacro

; ── print_u32: prints EBX as decimal ─────────────────────────
; Uses .u32buf (10 bytes) and trashes EAX,ECX,EDX,ESI.
print_u32:
    push ebp
    mov  ebp, esp
    sub  esp, 12             ; local: 10 B buf + 2 pad

    lea  edi, [ebp - 12]     ; buf start
    mov  byte [edi + 10], 0
    mov  ecx, 10             ; index = 10 (fill backward)
    mov  eax, ebx
    test eax, eax
    jnz  .digits
    dec  ecx
    mov  byte [edi + ecx], '0'
    jmp  .print
.digits:
    test eax, eax
    jz   .print
    xor  edx, edx
    mov  esi, 10
    div  esi                 ; EAX = quotient, EDX = remainder
    dec  ecx
    add  dl, '0'
    mov  [edi + ecx], dl
    jmp  .digits
.print:
    lea  ebx, [edi + ecx]    ; pointer to first digit
    mov  esi, 10
    sub  esi, ecx            ; length
    mov  eax, SYS_WRITE
    lea  edx, [.r]
    mov  ecx, esp
    sysenter
.r:
    mov  esp, ebp
    pop  ebp
    ret

; ── _start ───────────────────────────────────────────────────
_start:
    ; sys_fork() → EAX = child_pid (parent) or 0 (child)
    mov  eax, SYS_FORK
    lea  edx, [.after_fork]
    mov  ecx, esp
    sysenter
.after_fork:

    test eax, eax
    jz   .child_path

    ; ── parent ───────────────────────────────────────────────
    mov  esi, eax            ; save child_pid

    SYSWRITE msg_parent_fork, msg_parent_fork_len, .w1

    ; sys_waitpid(child_pid)
    mov  eax, SYS_WAITPID
    mov  ebx, esi
    lea  edx, [.after_wait]
    mov  ecx, esp
    sysenter
.after_wait:
    ; EAX = child exit code.  Save in EDI (untouched by SYSWRITE / sysenter
    ; since the macro only sets eax/ebx/esi/edx/ecx and the stub saves/restores edi).
    mov  edi, eax
    SYSWRITE msg_parent_wait, msg_parent_wait_len, .w2
    mov  ebx, edi
    call print_u32
    SYSWRITE msg_nl, 1, .w3

    SYSEXIT 0, .parent_done

    ; ── child ────────────────────────────────────────────────
.child_path:
    SYSWRITE msg_child, msg_child_len, .wc

    SYSEXIT 42, .child_done

section .data

msg_parent_fork:     db "parent: forked, waiting for child...", 10
msg_parent_fork_len: equ $ - msg_parent_fork

msg_parent_wait:     db "parent: child exited with code "
msg_parent_wait_len: equ $ - msg_parent_wait

msg_child:           db "child:  hello from fork!", 10
msg_child_len:       equ $ - msg_child

msg_nl:              db 10
