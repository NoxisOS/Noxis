; ─────────────────────────────────────────────────────────────
; asm/isr_stubs.asm — 64-bit exception entry stubs + common dispatcher.
;
; Each stub pushes a dummy error code (for vectors where the CPU does
; not), then the vector, then jumps to isr_common which saves the full
; register frame and calls isr_handler(isr_frame_t*).
; ─────────────────────────────────────────────────────────────
[BITS 64]

extern isr_handler
global isr_stub_table

%macro ISR_NOERR 1
isr_stub_%1:
    push 0
    push %1
    jmp  isr_common
%endmacro

%macro ISR_ERR 1
isr_stub_%1:
    push %1
    jmp  isr_common
%endmacro

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

isr_common:
    ; Save GP registers (rax first … r15 last) so RSP points at the
    ; isr_frame_t (r15 at lowest address).
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov  rdi, rsp            ; isr_frame_t* argument
    cld
    call isr_handler

    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  r11
    pop  r10
    pop  r9
    pop  r8
    pop  rbp
    pop  rdi
    pop  rsi
    pop  rdx
    pop  rcx
    pop  rbx
    pop  rax

    add  rsp, 16            ; pop vector + error code
    iretq

section .data
align 8
isr_stub_table:
%assign i 0
%rep 32
    dq isr_stub_ %+ i
%assign i i+1
%endrep
