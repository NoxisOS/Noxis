; ─────────────────────────────────────────────────────────────
; userland/signal.asm — ring-3 signal demo.
;
; 1. Install SIGUSR1 handler
; 2. Get our PID
; 3. Send SIGUSR1 to ourselves via kill()
; 4. Signal fires → handler prints, returns
; 5. Main prints confirmation, exits
;
; Syscall convention (sysenter):
;   EAX=#  EBX=arg1  ESI=arg2  EDI=arg3  EDX=retEIP  ECX=userESP
; ─────────────────────────────────────────────────────────────

section .text
[BITS 32]
global _start

%define SYS_EXIT      0
%define SYS_WRITE     1
%define SYS_SIGACTION 10
%define SYS_KILL      11
%define SYS_GETPID    12

%define SIGUSR1       10

; ── sysenter macro ──────────────────────────────────────────
%macro SYSENTER 0
    lea  edx, [%%ret]
    mov  ecx, esp
    sysenter
%%ret:
%endmacro

; ── _start ───────────────────────────────────────────────────
_start:
    call .here
.here:
    pop  ebp                     ; ebp = runtime &.here

    ; ── sigaction(SIGUSR1, &act, NULL) ────────────────────
    mov  eax, SYS_SIGACTION
    mov  ebx, SIGUSR1
    lea  esi, [ebp + sigact - .here]
    xor  edi, edi
    SYSENTER

    ; ── getpid() → push for later ─────────────────────────
    mov  eax, SYS_GETPID
    SYSENTER
    push eax                     ; [esp] = my pid

    ; ── print "sending SIGUSR1..." ────────────────────────
    mov  ebx, 1
    lea  esi, [ebp + msg_send - .here]
    mov  edi, msg_send_len
    mov  eax, SYS_WRITE
    SYSENTER

    ; ── kill(pid, SIGUSR1) ────────────────────────────────
    ;     signal delivered here → handler runs first
    ;     then handler returns back to here
    pop  ebx                     ; ebx = pid
    mov  esi, SIGUSR1
    mov  eax, SYS_KILL
    SYSENTER

    ; ── print "main: back after signal" ────────────────────
    mov  ebx, 1
    lea  esi, [ebp + msg_back - .here]
    mov  edi, msg_back_len
    mov  eax, SYS_WRITE
    SYSENTER

    ; ── exit(0) ───────────────────────────────────────────
    mov  eax, SYS_EXIT
    xor  ebx, ebx
    SYSENTER
    jmp  $                        ; unreachable

; ── signal handler: void handler(int signum) ──────────────────
;     signum is at [esp+4]
;     ret pops return addr (the instruction after kill call)
handler:
    push ebx
    push esi
    push edi

    call .getpc
.getpc:
    pop  ebp

    mov  ebx, 1
    lea  esi, [ebp + .hmsg - .getpc]
    mov  edi, .hmsg_len
    mov  eax, SYS_WRITE
    SYSENTER

    pop  edi
    pop  esi
    pop  ebx
    ret

.hmsg:       db "handler: got SIGUSR1!", 10
.hmsg_len:   equ $ - .hmsg

section .data

sigact:
    dd  handler       ; sighandler_t handler
    dd  0             ; flags (unused)

msg_send:     db "sending SIGUSR1 to self...", 10
msg_send_len: equ $ - msg_send

msg_back:     db "main: back after signal", 10
msg_back_len: equ $ - msg_back
