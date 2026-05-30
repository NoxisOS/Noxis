; ─────────────────────────────────────────────────────────────
; userland/ttytest.asm — TTY subsystem demo.
;
; Phase 1 — raw mode:
;   1. Set TTY to raw mode (ioctl TCSETS, lflag=0)
;   2. Read keystrokes one at a time, print each as hex
;   3. Press 'q' to quit, restore canonical mode
;
; Phase 2 — SIGINT test:
;   4. Install SIGINT handler
;   5. Do a blocking read (canonical mode)
;   6. Ctrl+C → handler fires, prints message, read returns -1
;
; Syscalls: SYS_IOCTL=13, TCGETS=1, TCSETS=2
; ─────────────────────────────────────────────────────────────

section .text
[BITS 32]
global _start

%define SYS_EXIT      0
%define SYS_WRITE     1
%define SYS_READ      2
%define SYS_SIGACTION 10
%define SYS_IOCTL     13

%define STDIN_FD      0
%define TCGETS        1
%define TCSETS        2
%define ISIG          1
%define ICANON        2
%define ECHO          4
%define SIGINT        2

%define RAW_MODE      (ISIG)          ; keep ISIG for Ctrl+C, drop ICANON+ECHO
%define CANON_MODE    (ISIG | ICANON | ECHO)

; ── sysenter macro ──────────────────────────────────────────
%macro SYSENTER 0
    lea  edx, [%%ret]
    mov  ecx, esp
    sysenter
%%ret:
%endmacro

; ── print_hex: prints AL as 2 hex chars ─────────────────────
print_hex_al:
    push eax
    push ebx
    push esi
    push edi

    sub  esp, 4
    mov  byte [esp], 0

    mov  bl, al
    shr  al, 4
    and  al, 0x0F
    cmp  al, 10
    jb   .h1
    add  al, 'A' - 10 - '0'
.h1:
    add  al, '0'
    mov  byte [esp], al

    mov  al, bl
    and  al, 0x0F
    cmp  al, 10
    jb   .h2
    add  al, 'A' - 10 - '0'
.h2:
    add  al, '0'
    mov  byte [esp+1], al

    mov  ebx, 1
    lea  esi, [esp]
    mov  edi, 2
    mov  eax, SYS_WRITE
    SYSENTER

    add  esp, 4
    pop  edi
    pop  esi
    pop  ebx
    pop  eax
    ret

; ── _start ───────────────────────────────────────────────────
_start:
    call .here
.here:
    pop  ebp

    ; ──────────── Phase 1: Raw mode ────────────────────────
    ; print banner
    mov  ebx, 1
    lea  esi, [ebp + msg_raw - .here]
    mov  edi, msg_raw_len
    mov  eax, SYS_WRITE
    SYSENTER

    ; set raw mode (ISIG only, no ICANON, no ECHO)
    sub  esp, 4
    mov  dword [esp], RAW_MODE
    mov  eax, SYS_IOCTL
    mov  ebx, STDIN_FD
    mov  esi, TCSETS
    mov  edi, esp
    SYSENTER
    add  esp, 4

    ; raw mode read loop
.raw_loop:
    mov  ebx, 1
    lea  esi, [ebp + msg_prompt - .here]
    mov  edi, msg_prompt_len
    mov  eax, SYS_WRITE
    SYSENTER

    ; read 1 char
    sub  esp, 4
    mov  eax, SYS_READ
    mov  ebx, STDIN_FD
    mov  esi, esp
    mov  edi, 1
    SYSENTER
    cmp  eax, 1
    jne  .raw_err

    mov  al, byte [esp]
    add  esp, 4

    cmp  al, 'q'
    je   .raw_done

    ; print hex
    call print_hex_al

    mov  ebx, 1
    lea  esi, [ebp + msg_nl - .here]
    mov  edi, 1
    mov  eax, SYS_WRITE
    SYSENTER

    jmp  .raw_loop

.raw_done:
    ; restore canonical mode
    sub  esp, 4
    mov  dword [esp], CANON_MODE
    mov  eax, SYS_IOCTL
    mov  ebx, STDIN_FD
    mov  esi, TCSETS
    mov  edi, esp
    SYSENTER
    add  esp, 4

    mov  ebx, 1
    lea  esi, [ebp + msg_raw_done - .here]
    mov  edi, msg_raw_done_len
    mov  eax, SYS_WRITE
    SYSENTER

    ; ──────────── Phase 2: SIGINT handler ──────────────────
    ; install handler
    mov  eax, SYS_SIGACTION
    mov  ebx, SIGINT
    lea  esi, [ebp + sigact - .here]
    xor  edi, edi
    SYSENTER

    ; print "waiting for Ctrl+C..."
    mov  ebx, 1
    lea  esi, [ebp + msg_wait - .here]
    mov  edi, msg_wait_len
    mov  eax, SYS_WRITE
    SYSENTER

    ; blocking read — interrupted by Ctrl+C
    sub  esp, 32
    mov  eax, SYS_READ
    mov  ebx, STDIN_FD
    mov  esi, esp
    mov  edi, 32
    SYSENTER
    add  esp, 32

    ; EAX = -1 if interrupted by signal, or bytes read
    cmp  eax, -1
    jne  .no_signal

    mov  ebx, 1
    lea  esi, [ebp + msg_eintr - .here]
    mov  edi, msg_eintr_len
    mov  eax, SYS_WRITE
    SYSENTER
    jmp  .done

.no_signal:
    mov  ebx, 1
    lea  esi, [ebp + msg_no_sig - .here]
    mov  edi, msg_no_sig_len
    mov  eax, SYS_WRITE
    SYSENTER

.done:
    mov  ebx, 1
    lea  esi, [ebp + msg_done - .here]
    mov  edi, msg_done_len
    mov  eax, SYS_WRITE
    SYSENTER

    mov  eax, SYS_EXIT
    xor  ebx, ebx
    SYSENTER
    jmp  $

.raw_err:
    add  esp, 4
    jmp  .done

; ── SIGINT handler ───────────────────────────────────────────
sig_handler:
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

.hmsg:       db "  SIGINT caught!", 10
.hmsg_len:   equ $ - .hmsg

section .data

sigact:
    dd  sig_handler
    dd  0

msg_raw:       db 10, "─── Raw mode test ───", 10, \
                  "Type keys to see raw scancodes (no echo, no line buffering)", 10, \
                  "Press 'q' to exit raw mode.", 10, 10
msg_raw_len:   equ $ - msg_raw

msg_prompt:    db "key> "
msg_prompt_len: equ $ - msg_prompt

msg_nl:        db 10

msg_raw_done:  db 10, "─── SIGINT test ───", 10
msg_raw_done_len: equ $ - msg_raw_done

msg_wait:      db "Press Ctrl+C...", 10
msg_wait_len:  equ $ - msg_wait

msg_eintr:     db "  read() interrupted by signal (EINTR)", 10
msg_eintr_len: equ $ - msg_eintr

msg_no_sig:    db "  no signal received", 10
msg_no_sig_len: equ $ - msg_no_sig

msg_done:      db "done.", 10
msg_done_len:  equ $ - msg_done
