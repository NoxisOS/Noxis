; ─────────────────────────────────────────────────────────────
; asm/tss_load.asm — Load Task Register (ltr)
; ─────────────────────────────────────────────────────────────

section .text
[BITS 32]

global tss_flush

tss_flush:
    mov  ax, 0x28              ; TSS selector (index 5, RPL 0)
    ltr  ax
    ret
