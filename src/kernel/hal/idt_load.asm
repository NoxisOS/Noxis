; ─────────────────────────────────────────────────────────────
; asm/idt_load.asm — lidt wrapper (64-bit).
; ─────────────────────────────────────────────────────────────
[BITS 64]
global idt64_load

; void idt64_load(struct idt_ptr* p)   — RDI = &{limit, base}
idt64_load:
    lidt [rdi]
    ret
