; ─────────────────────────────────────────────────────────────
; asm/msr.asm — Model-Specific Register read/write
; ─────────────────────────────────────────────────────────────

section .text
[BITS 32]

global msr_read
global msr_write

; uint64_t msr_read(uint32_t msr);
; Returns value in EDX:EAX
msr_read:
    mov  ecx, [esp + 4]    ; MSR index
    rdmsr
    ret

; void msr_write(uint32_t msr, uint32_t low, uint32_t high);
msr_write:
    mov  ecx, [esp + 4]    ; MSR index
    mov  eax, [esp + 8]    ; low 32 bits
    mov  edx, [esp + 12]   ; high 32 bits
    wrmsr
    ret
