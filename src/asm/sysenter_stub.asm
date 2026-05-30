; ─────────────────────────────────────────────────────────────
; asm/sysenter_stub.asm — sysenter/sysexit kernel handler
;
; Entry from user (ring 3) via `sysenter`:
;   CPU loads CS = MSR_SYSENTER_CS, SS = CS+8, EIP = MSR_SYSENTER_EIP,
;            ESP = MSR_SYSENTER_ESP, clears IF.
;   User convention: EAX=syscall#, EBX=arg1, ESI=arg2,
;                    EDX=return EIP, ECX=user ESP.
;
; We build a layout that matches isr_frame_t exactly so the same
; C dispatcher (syscall_handler) used by int 0x80 can read it.
;
; isr_frame_t layout (low → high):
;   gs, fs, es, ds,
;   edi, esi, ebp, esp_val, ebx, edx, ecx, eax,
;   vector, error_code,
;   eip, cs, eflags, user_esp, user_ss
; ─────────────────────────────────────────────────────────────

section .text
[BITS 32]

global sysenter_entry
extern syscall_handler

sysenter_entry:
    ; Build the frame from the TOP (high addr) down. Each push decrements ESP,
    ; so the LAST push lands at offset 0 of the frame — which must be `gs`.

    ; ── trailing (CPU-frame-like) fields, highest addresses ──────
    push dword 0x23          ; user_ss
    push ecx                 ; user_esp (saved from user's ECX)
    pushfd                   ; eflags
    or   dword [esp], 0x200  ; force IF in saved eflags (user resumes with IF=1)
    push dword 0x1B          ; cs (user code, RPL3)
    push edx                 ; eip (sysexit return target, saved from user's EDX)

    ; ── stub-pushed fields ──────────────────────────────────────
    push dword 0             ; error_code
    push dword 128           ; vector (matches int 0x80 number)

    ; ── pushad-equivalent: EAX first (highest), EDI last (lowest) ─
    push eax                 ; eax  = syscall #
    push dword 0             ; ecx placeholder (user's ECX was the saved user ESP)
    push dword 0             ; edx placeholder (user's EDX was the return EIP)
    push ebx                 ; ebx  = arg1 (e.g. msg pointer)
    push dword 0             ; esp_val placeholder
    push ebp                 ; ebp
    push esi                 ; esi  = arg2
    push edi                 ; edi  = arg3 (e.g. sys_read length)

    ; ── segment regs, gs LAST → lands at offset 0 ───────────────
    push dword 0x23          ; ds
    push dword 0x23          ; es
    push dword 0x23          ; fs
    push dword 0x23          ; gs

    ; sysenter clears IF — re-enable so blocking syscalls
    ; (SYS_READ → kbd_getchar → hlt) peuvent être réveillés par les IRQs.
    sti

    ; Call C dispatcher with pointer to frame
    push esp
    call syscall_handler
    add  esp, 4

    ; Disable interrupts before restoring user state + sysexit
    cli

    ; ── tear down frame ─────────────────────────────────────────
    add  esp, 16             ; skip gs, fs, es, ds
    pop  edi
    pop  esi
    pop  ebp
    add  esp, 4              ; skip esp_val
    pop  ebx
    add  esp, 8              ; skip edx, ecx placeholders
    pop  eax                 ; syscall return value (handler wrote frame->eax)
    add  esp, 8              ; skip vector, error_code
    pop  edx                 ; return EIP   → goes to sysexit
    add  esp, 4              ; skip cs
    popfd                    ; restore EFLAGS (IF already set above)
    pop  ecx                 ; user ESP     → goes to sysexit
    add  esp, 4              ; skip user_ss

    sysexit                  ; CS=USER_CS, SS=USER_DS, EIP=EDX, ESP=ECX
