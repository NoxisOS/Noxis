; ─────────────────────────────────────────────────────────────
; asm/kernel_entry.asm — Kernel entry: setup paging, jump to higher-half
;
; Purpose: First code at physical 0x100000. Sets up 2-level
;          paging (identity map 0-4MB + kernel at 0xC0000000),
;          enables paging, far-jumps to higher-half, then
;          calls kernel_main() at virtual address.
;
; Entry:  Bootloader jumps here at physical 0x100000.
;         CPU: 32-bit protected mode, paging disabled.
;         Segments: CS=0x08, DS/ES/FS/GS/SS=0x10 (flat 4GB).
;
; Page structures (hardcoded physical addresses):
;   0x400000 — Page Directory (4 KB)
;   0x401000 — Page Table 0: identity-map 0-4 MB
;   0x402000 — Kernel PT: map 0xC0000000 → physical 0x00000000
;
; Exit:   Calls kernel_main() at higher-half, never returns.
; ─────────────────────────────────────────────────────────────

%define PD_PHYS          0x400000
%define PT0_PHYS         0x401000
%define PTK_PHYS         0x402000
%define TEMP_STACK       0x20000
%define PHYS_TO_VIRT(v)  ((v) + 0xC0000000)

section .text.entry
[BITS 32]

global _start
extern vmm_init
extern load_cr3
extern enable_paging
extern kernel_main
extern _bss_start
extern _bss_end

_start:
    ; Segment registers (already set, but redo for safety)
    mov  ax, 0x10
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax

    ; ── Temporary stack in low memory ────────────────────────
    mov  esp, TEMP_STACK

    ; ── Zero BSS (using physical addresses) ──────────────────
    ; _bss_start and _bss_end are virtual addresses (0xC01xxxxx).
    ; Before paging, physical = virtual - 0xC0000000.
    mov  edi, _bss_start
    sub  edi, 0xC0000000
    mov  ecx, _bss_end
    sub  ecx, 0xC0000000
    sub  ecx, edi               ; BSS size in bytes
    jz   .bss_done
    shr  ecx, 2                 ; dword count
    xor  eax, eax
    cld
    rep  stosd
.bss_done:

    ; ── Initialize page tables (C function, identity-mapped) ─
    push dword PTK_PHYS
    push dword PT0_PHYS
    push dword PD_PHYS
    call vmm_init
    add  esp, 12

    ; ── Load CR3, enable paging ─────────────────────────────
    push dword PD_PHYS
    call load_cr3
    add  esp, 4

    call enable_paging

    ; ── Far jump to higher-half ──────────────────────────────
    ; After paging: 0xC0100000 virtual → 0x100000 physical
    jmp  0x08:higher_half

higher_half:
    ; Reload segment registers with kernel data selector
    mov  ax, 0x10
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax

    ; ── Kernel stack at virtual address ─────────────────────
    mov  esp, _kernel_stack_top

    ; ── Call C kernel main (at higher-half) ────────────────
    call kernel_main

.halt_loop:
    cli
    hlt
    jmp  .halt_loop

; ── Kernel stack (16 KB, in BSS) ────────────────────────────
section .bss
align 16
_kernel_stack_bottom:
    resb 16384
_kernel_stack_top:
