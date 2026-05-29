; ─────────────────────────────────────────────────────────────
; asm/syscall_stub.asm — int 0x80 stub and dispatcher
; ─────────────────────────────────────────────────────────────

section .text
[BITS 32]

global isr_stub_128
extern isr_common
extern syscall_handler

; int 0x80 entry — CPU pushes EIP, CS, EFLAGS, ESP, SS
; We push dummy error + vector, then go through isr_common
isr_stub_128:
    push dword 0                  ; dummy error code
    push dword 128                ; vector 0x80
    jmp  isr_common
