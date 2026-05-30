; ─────────────────────────────────────────────────────────────
; asm/kthread_switch.asm — Kernel thread context switch
;
; void kthread_switch(uint32_t* old_esp, uint32_t* new_esp);
;
; Saves callee-saved registers + ESP on old stack, loads new ESP,
; restores callee-saved from new stack, returns into new thread.
;
; When a thread is first switched TO, its stack was pre-initialized
; by proc_spawn to look like kthread_switch saved it just before a
; ret to the entry function.
;
; Stack layout after pushes (offsets from saved ESP):
;   [esp+0]  ebx   ← popped first on restore
;   [esp+4]  edi
;   [esp+8]  esi
;   [esp+12] ebp
;   [esp+16] return address  (where kthread_switch returns to)
; ─────────────────────────────────────────────────────────────

section .text
[BITS 32]

global kthread_switch
global kthread_entry           ; entry trampoline for new threads

; ── kthread_entry ─────────────────────────────────────────────
; Every new kernel thread lands here on its FIRST run.
; At this point we're still "inside" the PIT ISR context (IF=0).
; Re-enable interrupts, then jump to the actual entry function
; (its address is the next value on the stack from the pre-init).
kthread_entry:
    sti                          ; re-enable IRQs — PIT can now fire again
    ret                          ; pop entry fn addr and jump to it

kthread_switch:
    ; Save callee-saved registers on current stack
    push ebp
    push esi
    push edi
    push ebx

    ; *old_esp = esp
    mov  eax, [esp + 20]    ; old_esp param
    mov  [eax], esp

    ; esp = *new_esp
    mov  eax, [esp + 24]    ; new_esp param — NOTE: still on OLD stack here,
                             ; so old_esp is at +20 and new_esp at +24.
                             ; After `mov esp, [eax]` we're on the new stack.
    mov  esp, [eax]

    ; Restore callee-saved registers from new stack
    pop  ebx
    pop  edi
    pop  esi
    pop  ebp

    ret                      ; return into new thread's saved call site
