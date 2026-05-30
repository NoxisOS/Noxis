; ─────────────────────────────────────────────────────────────
; asm/kjmp.asm — kernel-side setjmp / longjmp
;
; Used by exec_run() to save the shell's kernel context before
; iret-ing to ring 3. sys_exit longjmps back, unwinding both
; the sysenter stack and the user mode in one shot.
;
; jmp_buf layout (28 bytes):
;   0   ebx
;   4   esi
;   8   edi
;   12  ebp
;   16  esp     (caller's esp after the call would return)
;   20  eip     (the address kjmp_save would `ret` to)
;   24  eflags
; ─────────────────────────────────────────────────────────────

section .text
[BITS 32]

global kjmp_save
global kjmp_restore

; int kjmp_save(uint32_t* buf);
;   Returns 0 on direct call, nonzero when reached via kjmp_restore.
kjmp_save:
    mov  eax, [esp + 4]          ; buf
    mov  [eax + 0],  ebx
    mov  [eax + 4],  esi
    mov  [eax + 8],  edi
    mov  [eax + 12], ebp

    ; saved esp = caller's esp just AFTER our ret would pop the return addr
    lea  edx, [esp + 4]
    mov  [eax + 16], edx

    ; saved eip = our return address
    mov  edx, [esp]
    mov  [eax + 20], edx

    pushfd
    pop  edx
    mov  [eax + 24], edx

    xor  eax, eax                ; first return: 0
    ret

; void kjmp_restore(uint32_t* buf, int val);
;   Never returns to its own caller — resumes inside the function that
;   originally called kjmp_save, with eax = val (forced nonzero).
kjmp_restore:
    mov  eax, [esp + 4]          ; buf
    mov  ecx, [esp + 8]          ; val

    ; Restore callee-saved registers
    mov  ebx, [eax + 0]
    mov  esi, [eax + 4]
    mov  edi, [eax + 8]
    mov  ebp, [eax + 12]

    ; Jump to the saved stack
    mov  esp, [eax + 16]

    ; Build a (eip, eflags) frame on the restored stack so we can
    ; popfd then ret in one clean sequence.
    push dword [eax + 20]        ; saved EIP (below)
    push dword [eax + 24]        ; saved EFLAGS (top)

    ; Force the longjmp return value to be nonzero
    test ecx, ecx
    jnz  .have_val
    mov  ecx, 1
.have_val:
    mov  eax, ecx

    popfd                        ; restore EFLAGS
    ret                          ; pop EIP → resume in kjmp_save's caller
