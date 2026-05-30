; ─────────────────────────────────────────────────────────────
; asm/idt_load.asm — Load the IDT register
;
; Purpose: Executes lidt with the given IDT pointer.
;          Called once during kernel initialization.
; ─────────────────────────────────────────────────────────────

section .text
[BITS 32]

global idt_flush

; void idt_flush(idt_ptr_t* ptr);
idt_flush:
    mov  eax, [esp + 4]            ; pointer to idt_ptr_t
    lidt [eax]                     ; load IDT
    ret
