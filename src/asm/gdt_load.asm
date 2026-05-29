; ─────────────────────────────────────────────────────────────
; asm/gdt_load.asm — Load the GDT and reload segment registers
;
; Purpose: Executes lgdt, far-jumps to reload CS, then reloads
;          all data segment registers with the kernel DS selector.
;
; Entry:  [esp+4] = pointer to gdt_ptr_t structure
; Exit:   All segment registers reloaded
; ─────────────────────────────────────────────────────────────

section .text
[BITS 32]

global gdt_flush

gdt_flush:
    mov  eax, [esp + 4]            ; pointer to gdt_ptr_t
    lgdt [eax]                     ; load GDT

    ; Far jump to reload CS with kernel code selector (0x08)
    jmp  0x08:.reload_cs

.reload_cs:
    ; Reload all data segments with kernel data selector (0x10)
    mov  ax, 0x10
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax
    ret
