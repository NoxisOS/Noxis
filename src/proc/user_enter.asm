; ─────────────────────────────────────────────────────────────
; asm/user_enter.asm — Jump to user mode (ring 3) via iret
;
;   void user_enter(uint32_t entry, uint32_t stack_top);
;
; Builds an iret frame (SS, ESP, EFLAGS, CS, EIP) for ring 3
; with IF=1 so the user is interruptible.
;
; Selectors:
;   0x1B = GDT[3] (USER_CS) with RPL=3
;   0x23 = GDT[4] (USER_DS) with RPL=3
; ─────────────────────────────────────────────────────────────

section .text
[BITS 32]

global user_enter

user_enter:
    ; Snapshot params before any push moves esp
    mov  edx, [esp + 4]          ; entry
    mov  ecx, [esp + 8]          ; user stack top

    ; iret frame (top of stack → EIP)
    push dword 0x23              ; SS  user data
    push ecx                     ; ESP user stack
    pushfd                       ; EFLAGS
    pop   eax
    or    eax, 0x200             ; enable IF
    push  eax
    push dword 0x1B              ; CS  user code
    push edx                     ; EIP entry

    ; Reload user data segments
    mov  ax, 0x23
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax

    iret                          ; → ring 3
