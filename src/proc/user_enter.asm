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
global user_enter_fork      ; same as user_enter but sets EAX=0 (fork child)

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

; ── user_enter_fork ─────────────────────────────────────────
; Like user_enter but restores the parent's saved register state so
; that the fork child resumes with all user registers intact.
; EAX is zeroed so fork() returns 0 in the child.
;
; void user_enter_fork(uint32_t entry, uint32_t stack,
;                      uint32_t ebp,   uint32_t ebx,
;                      uint32_t esi,   uint32_t edi);
user_enter_fork:
    ; Load args before touching stack.
    mov  edx, [esp + 4]          ; entry (ring-3 EIP)
    mov  ecx, [esp + 8]          ; user stack (ring-3 ESP)
    mov  ebp, [esp + 12]         ; parent's EBP to restore
    mov  ebx, [esp + 16]         ; parent's EBX to restore
    mov  esi, [esp + 20]         ; parent's ESI to restore
    mov  edi, [esp + 24]         ; parent's EDI to restore

    push dword 0x23              ; SS  user data
    push ecx                     ; ESP user stack
    pushfd                       ; EFLAGS
    pop   eax
    or    eax, 0x200             ; enable IF
    push  eax
    push dword 0x1B              ; CS  user code
    push edx                     ; EIP entry

    mov  ax, 0x23
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax

    xor  eax, eax                ; fork() returns 0 in the child
    iret                          ; → ring 3 with all registers restored
