; ─────────────────────────────────────────────────────────────
; userland/echo.asm — print argv[1..] space-separated, then newline.
;
; Stack on entry (built by exec_run / _build_argv_frame):
;   [esp+0]  argc        (uint32)
;   [esp+4]  argv[0]     pointer (program name)
;   [esp+8]  argv[1]     pointer (first real arg)
;   ...
;
; Sysenter convention:
;   EAX = syscall#    EBX = arg1    ESI = arg2    EDI = arg3
;   EDX = return EIP  ECX = user ESP
;
; EBX, ESI, EDI, EBP are callee-saved across sysenter (the stub
; saves and restores them), so we can use them freely between calls.
; ─────────────────────────────────────────────────────────────

section .text
[BITS 32]
global _start

%define SYS_EXIT  0
%define SYS_WRITE 1

; ── Macro: call sys_write(EBX=buf, ESI=len) and return to .lbl ──
%macro WRITE 1
    mov  eax, SYS_WRITE
    lea  edx, [%1]
    mov  ecx, esp
    sysenter
%1:
%endmacro

; ── strlen(eax) → ecx ───────────────────────────────────────
_strlen:
    xor  ecx, ecx
.l: cmp  byte [eax+ecx], 0
    je   .done
    inc  ecx
    jmp  .l
.done:
    ret

; ── _start ──────────────────────────────────────────────────
_start:
    mov  ebp, esp              ; freeze the stack pointer (we'll read args via ebp)

    mov  edi, [ebp]            ; edi = argc
    cmp  edi, 2
    jb   .nl                  ; no arguments at all → just print newline

    mov  ebx, 1               ; i = 1  (argv[0] is the program name, skip it)

.loop:
    cmp  ebx, edi             ; i >= argc ?
    jae  .nl

    ; print space between words (not before the first)
    cmp  ebx, 1
    je   .no_space
    push ebx                  ; save i (callee-saved across sysenter but
    push edi                  ; be explicit — saved by stub anyway)
    lea  ebx, [_sp]
    mov  esi, 1
    WRITE .r_sp
    pop  edi
    pop  ebx
.no_space:

    ; print argv[i]
    push ebx
    push edi
    mov  eax, [ebp + 4 + ebx*4]  ; argv[i]
    call _strlen                   ; → ecx = len
    mov  esi, ecx
    mov  ebx, eax
    WRITE .r_arg
    pop  edi
    pop  ebx

    inc  ebx
    jmp  .loop

.nl:
    lea  ebx, [_nl]
    mov  esi, 1
    WRITE .r_nl

    xor  ebx, ebx             ; exit code 0
    mov  eax, SYS_EXIT
    lea  edx, [.done]
    mov  ecx, esp
    sysenter
.done: jmp $

section .data
_sp: db ' '
_nl: db 10
