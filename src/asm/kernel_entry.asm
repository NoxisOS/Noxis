; ─────────────────────────────────────────────────────────────
; asm/kernel_entry.asm — Kernel entry trampoline
;
; Purpose: First code executed after the bootloader jumps to the
;          kernel at 0x100000. Sets up segment registers, zeroes
;          BSS (using a temporary stack to avoid self-clobber),
;          then calls kernel_main().
;
; Entry:  Bootloader jumps here at physical 0x100000.
;         CPU is in 32-bit protected mode, paging disabled.
;         Segment registers are already set to KERNEL_DS (0x10).
;
; Exit:   Calls kernel_main(), which never returns.
;
; Register convention:
;   cdecl: EBX, ESI, EDI, EBP, ESP preserved
;          EAX, ECX, EDX, EFLAGS may be destroyed
; ─────────────────────────────────────────────────────────────

section .text.entry
[BITS 32]

; ── Exports ──────────────────────────────────────────────────
global _start
extern kernel_main

; ── Linker symbols ───────────────────────────────────────────
extern _bss_start
extern _bss_end

; ── Entry point ──────────────────────────────────────────────
_start:
    ; Segment registers are already set by the bootloader.
    ; Re-set them to be absolutely sure.
    mov  ax, 0x10                   ; kernel data segment selector
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax

    ; ── Set up a temporary stack ─────────────────────────────
    ; We cannot use the BSS-based kernel stack yet because it
    ; hasn't been zeroed. Using a location in low memory that
    ; is safe (no BIOS active in protected mode).
    mov  esp, 0x20000

    ; ── Zero the BSS section ─────────────────────────────────
    ; This is done inline (no call/ret) because the BSS contains
    ; the kernel stack itself. Using call/ret would place the
    ; return address on the BSS stack, then rep stosd would
    ; overwrite it, causing a crash on ret.
    mov  edi, _bss_start
    mov  ecx, _bss_end
    sub  ecx, edi                   ; BSS size in bytes
    jz   .bss_done                  ; skip if BSS is empty
    shr  ecx, 2                     ; dword count
    xor  eax, eax                   ; zero fill value
    cld
    rep  stosd                      ; fill BSS with zeros
.bss_done:

    ; ── Switch to kernel stack ───────────────────────────────
    mov  esp, _kernel_stack_top

    ; ── Call C kernel main ───────────────────────────────────
    call kernel_main

    ; If kernel_main ever returns, halt forever
.halt:
    cli
    hlt
    jmp  .halt

; ── Kernel stack (16 KB, zeroed by rep stosd above) ──────────
section .bss
align 16
_kernel_stack_bottom:
    resb 16384                      ; 16 KB kernel stack
_kernel_stack_top:
