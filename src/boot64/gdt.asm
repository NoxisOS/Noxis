; ─────────────────────────────────────────────────────────────
; src/boot64/gdt.asm — load the 64-bit GDT and reload segments + TSS.
; ─────────────────────────────────────────────────────────────
[BITS 64]

global gdt64_load
global tss64_load

; void gdt64_load(struct gdt_ptr* p)   — RDI = pointer to {limit, base}
gdt64_load:
    lgdt [rdi]
    ; Reload data segment registers with the kernel data selector (0x10).
    mov  ax, 0x10
    mov  ds, ax
    mov  es, ax
    mov  ss, ax
    mov  fs, ax
    mov  gs, ax
    ; Reload CS via a far return (can't mov cs directly in long mode).
    lea  rax, [rel .reload_cs]
    push 0x08                  ; kernel code selector
    push rax
    retfq                       ; far return → loads CS = 0x08
.reload_cs:
    ret

; void tss64_load(uint16_t sel)   — DI = TSS selector
tss64_load:
    mov  ax, di
    ltr  ax
    ret
