; ─────────────────────────────────────────────────────────────
; asm/paging.asm — Paging control operations
;
; Purpose: Load CR3, enable paging (CR0.PG), TLB invalidation.
;          Called from kernel_entry.asm during boot.
; ─────────────────────────────────────────────────────────────

section .text
[BITS 32]

global load_cr3
global enable_paging
global vmm_invlpg

; void load_cr3(uint32_t pd_phys);
load_cr3:
    mov  eax, [esp + 4]            ; physical address of page directory
    mov  cr3, eax                  ; load page directory base
    ret

; void enable_paging(void);
; Assumes CR3 is already loaded and identity mapping is in place.
enable_paging:
    mov  eax, cr0
    or   eax, 0x80000000           ; set PG bit (bit 31)
    mov  cr0, eax
    ret

; void vmm_invlpg(uint32_t virt);
vmm_invlpg:
    mov  eax, [esp + 4]
    invlpg [eax]
    ret
