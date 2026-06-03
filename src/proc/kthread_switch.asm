; ─────────────────────────────────────────────────────────────
; asm/kthread_switch.asm — 64-bit kernel-thread context switch.
;
; void kthread_switch(uint64_t* save_old_rsp, uint64_t new_rsp);
;   RDI = where to store the outgoing thread's RSP
;   RSI = the incoming thread's saved RSP
;
; Saves the SysV callee-saved registers, swaps stacks, restores them,
; and returns into the new thread.  A freshly-spawned thread's stack is
; pre-built (see process.c) so the final `ret` jumps to its entry point.
; ─────────────────────────────────────────────────────────────
[BITS 64]
global kthread_switch
global thread_trampoline

kthread_switch:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    mov  [rdi], rsp        ; save outgoing RSP
    mov  rsp, rsi          ; load incoming RSP
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  rbp
    pop  rbx
    ret

; First entry of a freshly-spawned thread.  proc_spawn primes RBX with the
; real entry point and makes this the `ret` target.  We enable interrupts
; (a new thread is reached with IF=0 since it never iret'd) then call it.
thread_trampoline:
    sti
    call rbx
.hang:
    cli
    hlt
    jmp .hang
