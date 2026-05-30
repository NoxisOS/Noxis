; ─────────────────────────────────────────────────────────────
; userland/init.asm — ring-3 init process: a tiny interactive shell.
;
;   Builtins:  help, clear
;   Anything else → fork() + execve(<name>) + waitpid()  (classic shell)
;
; Addressing is ABSOLUTE: elf_load maps each segment at its link address
; (0x400000), so label addresses are valid as-is.  This deliberately
; avoids PIC/ebp tricks — a fork child does not inherit ebp (user_enter_fork
; only zeroes eax), so relying on ebp after fork would be a footgun.
;
; sysenter convention: EAX=#, EBX=arg1, ESI=arg2, EDI=arg3,
;                      EDX=return EIP, ECX=user ESP.
; The kernel preserves EBX/ESI/EDI/EBP across the call; EAX=result.
; ─────────────────────────────────────────────────────────────

section .text
[BITS 32]
global _start

%define SYS_EXIT    0
%define SYS_WRITE   1
%define SYS_READ    2
%define SYS_FORK    5
%define SYS_WAITPID 6
%define SYS_EXECVE  19

; Perform a sysenter.  Sets EDX=return target and ECX=ESP, then enters
; the kernel; sysexit lands on the unique %%ret label.  For SYS_FORK the
; child resumes here too (its EIP == EDX, its ESP is a copy of ours).
%macro SYS 0
    lea  edx, [%%ret]
    mov  ecx, esp
    sysenter
%%ret:
%endmacro

_start:
    mov  esi, banner
    call print

.loop:
    mov  esi, prompt
    call print

    ; read a line from stdin (fd 0)
    mov  eax, SYS_READ
    mov  ebx, 0
    mov  esi, linebuf
    mov  edi, 127
    SYS
    test eax, eax
    jle  .loop

    ; null-terminate and strip a trailing newline
    mov  ecx, eax
    mov  byte [linebuf + ecx], 0
    cmp  byte [linebuf + ecx - 1], 10
    jne  .nonl
    mov  byte [linebuf + ecx - 1], 0
.nonl:
    cmp  byte [linebuf], 0
    je   .loop

    ; builtin: help
    mov  esi, linebuf
    mov  edi, s_help
    call streq
    test eax, eax
    jnz  .not_help
    mov  esi, helpmsg
    call print
    jmp  .loop
.not_help:

    ; builtin: clear
    mov  esi, linebuf
    mov  edi, s_clear
    call streq
    test eax, eax
    jnz  .not_clear
    mov  esi, clrseq
    call print
    jmp  .loop
.not_clear:

    ; external program: fork + execve + waitpid
    mov  eax, SYS_FORK
    SYS
    test eax, eax
    jz   .child

    ; parent: wait for the child to finish, then loop
    mov  ebx, eax
    mov  eax, SYS_WAITPID
    SYS
    jmp  .loop

.child:
    mov  eax, SYS_EXECVE
    mov  ebx, linebuf
    SYS
    ; execve only returns on failure
    mov  esi, execfail
    call print
    mov  eax, SYS_EXIT
    mov  ebx, 1
    SYS
.hang:
    jmp  .hang

; ── print: ESI = null-terminated string ─────────────────────
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

; ── streq: ESI, EDI → EAX=0 if equal, 1 otherwise ───────────
streq:
    push esi
    push edi
.cmp:
    mov  al, [esi]
    mov  ah, [edi]
    cmp  al, ah
    jne  .diff
    test al, al
    jz   .same
    inc  esi
    inc  edi
    jmp  .cmp
.diff:
    pop  edi
    pop  esi
    mov  eax, 1
    ret
.same:
    pop  edi
    pop  esi
    xor  eax, eax
    ret

; ── data ─────────────────────────────────────────────────────
section .data
banner:
    db 10
    db "  N O X I S   O S    v 0 . 9 . 0   (ring-3 init)", 10
    db "  type 'help' for commands", 10, 0
prompt:
    db 10, "nxs$ ", 0
helpmsg:
    db 10
    db "  help   - this message", 10
    db "  clear  - clear the screen", 10
    db 10
    db "  run a program by name, e.g.:", 10
    db "    hello.elf  fork.elf  pipe.elf  pftest.elf", 10
    db "    segv.elf   brktest.elf", 10, 0
execfail:
    db "  no such program", 10, 0
clrseq:
    db 27, "[2J", 27, "[H", 0
s_help:   db "help", 0
s_clear:  db "clear", 0

; ── bss ──────────────────────────────────────────────────────
section .bss
linebuf: resb 256
