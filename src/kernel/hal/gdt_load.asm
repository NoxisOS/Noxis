; ─────────────────────────────────────────────────────────────
; asm/gdt_load.asm — load the 64-bit GDT, reload segments + TSS.
; ─────────────────────────────────────────────────────────────
[BITS 64]

global gdt64_load
global tss64_load

; void gdt64_load(struct gdt_ptr* p)   — RDI = &{limit, base}
gdt64_load:
    lgdt [rdi]
    mov  ax, 0x10
    mov  ds, ax
    mov  es, ax
    mov  ss, ax
    mov  fs, ax
    mov  gs, ax
    lea  rax, [rel .reload_cs]
    push 0x08
    push rax
    retfq                        ; far return → CS = 0x08
.reload_cs:
    ret

; void tss64_load(uint16_t sel)   — DI = selector
tss64_load:
    mov  ax, di
    ltr  ax
    ret
