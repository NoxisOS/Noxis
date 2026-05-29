; ─────────────────────────────────────────────────────────────
; asm/user_enter.asm — Jump to user mode (ring 3) via iret
; ─────────────────────────────────────────────────────────────

section .text
[BITS 32]

global user_enter

; void user_enter(uint32_t entry, uint32_t stack);
user_enter:
    ; Snapshot params before any push moves esp
    mov  edx, [esp + 4]          ; entry
    mov  ecx, [esp + 8]          ; user stack top

    ; Build iret frame: SS, ESP, EFLAGS, CS, EIP (top → EIP)
    push dword 0x23              ; SS = user data selector (index 4, RPL 3)
    push ecx                     ; ESP = user stack
    pushfd                       ; EFLAGS
    pop   eax
    or    eax, 0x200             ; enable IF
    push eax
    push dword 0x1B              ; CS = user code selector (index 3, RPL 3)
    push edx                     ; EIP = entry

    ; Set user data segs for ring 3
    mov  ax, 0x23
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax

    iret                          ; jump to ring 3
