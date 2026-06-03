; ─────────────────────────────────────────────────────────────
; src/boot64/idt_load.asm — lidt wrapper.
; ─────────────────────────────────────────────────────────────
[BITS 64]
global idt64_load

; void idt64_load(struct idt_ptr* p)   — RDI = pointer to {limit, base}
idt64_load:
    lidt [rdi]
    ret
