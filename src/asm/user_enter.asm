; ─────────────────────────────────────────────────────────────
; asm/user_enter.asm — Jump to user mode (ring 3) via iret
; ─────────────────────────────────────────────────────────────

section .text
[BITS 32]

global user_enter

; void user_enter(uint32_t entry, uint32_t stack);
user_enter:
    ; Set up segment registers for user mode return
    ; iret will pop: EIP, CS, EFLAGS, ESP, SS
    push dword 0x23              ; SS = user data selector (index 4, RPL 3)
    push dword [esp + 8]         ; ESP = user stack (parameter)
    pushfd                       ; EFLAGS
    pop   eax
    or    eax, 0x200             ; enable IF
    push eax
    push dword 0x1B              ; CS = user code selector (index 3, RPL 3)
    push dword [esp + 24]        ; EIP = entry (parameter, adjusted for pushes)

    ; Set DS for user mode
    mov  ax, 0x23
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax

    iret                          ; jump to ring 3
