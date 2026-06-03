; ─────────────────────────────────────────────────────────────
; src/boot64/isr.asm — 64-bit exception stubs + common dispatcher.
;
; Some exceptions push an error code, others don't.  We normalise the
; stack so the C handler always sees the same frame: stubs without an
; error code push a dummy 0.  Then we push the vector number.
; ─────────────────────────────────────────────────────────────
[BITS 64]

extern isr_dispatch          ; void isr_dispatch(uint64_t vec, uint64_t err)
global isr_stub_table

%macro ISR_NOERR 1
isr_stub_%1:
    push 0                    ; dummy error code
    push %1                   ; vector
    jmp  isr_common
%endmacro

%macro ISR_ERR 1
isr_stub_%1:
    ; CPU already pushed the error code
    push %1                   ; vector
    jmp  isr_common
%endmacro

; Vectors with a CPU error code: 8, 10-14, 17, 21
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

; Stack at entry to isr_common (top → bottom):
;   [rsp+0]  vector
;   [rsp+8]  error code
;   [rsp+16] RIP / CS / RFLAGS / RSP / SS  (CPU-pushed)
isr_common:
    ; vector and error code are the two C arguments (RDI, RSI)
    mov  rdi, [rsp]           ; vector
    mov  rsi, [rsp + 8]       ; error code

    ; Save the scratch registers the SysV ABI lets the callee clobber
    push rax
    push rcx
    push rdx
    push r8
    push r9
    push r10
    push r11

    call isr_dispatch

    pop  r11
    pop  r10
    pop  r9
    pop  r8
    pop  rdx
    pop  rcx
    pop  rax

    add  rsp, 16              ; pop vector + error code
    iretq

; Table of stub addresses, indexed by vector (used by idt.c).
section .data
align 8
isr_stub_table:
%assign i 0
%rep 32
    dq isr_stub_ %+ i
%assign i i+1
%endrep
