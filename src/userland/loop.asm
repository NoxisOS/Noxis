; userland/loop.asm — infinite loop: prints dots every ~500ms
; Used to test Ctrl+C signal delivery to a child process.

section .text
[BITS 32]
global _start

%define SYS_EXIT   0
%define SYS_WRITE  1
%define SYS_SLEEP  25

%macro SYS 0
    lea  edx, [%%ret]
    mov  ecx, esp
    sysenter
%%ret:
%endmacro

_start:
    mov  esi, msg_start
    call print

.loop:
    ; write "."
    mov  eax, SYS_WRITE
    mov  ebx, 1          ; stdout
    mov  esi, dot
    mov  edi, 1
    SYS

    ; sleep 500 ms
    mov  eax, SYS_SLEEP
    mov  ebx, 500
    SYS

    jmp  .loop

    ; unreachable
    mov  eax, SYS_EXIT
    xor  ebx, ebx
    SYS

print:
    xor  ecx, ecx
.len:
    cmp  byte [esi + ecx], 0
    je   .out
    inc  ecx
    jmp  .len
.out:
    mov  eax, SYS_WRITE
    mov  ebx, 1
    mov  edi, ecx
    SYS
    ret

section .data
msg_start: db "loop: running (Ctrl+C to kill)", 10, 0
dot:       db "."
