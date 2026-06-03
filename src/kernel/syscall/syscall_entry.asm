; ─────────────────────────────────────────────────────────────
; kernel/syscall/syscall_entry.asm — SYSCALL instruction entry (x86-64).
;
; On SYSCALL the CPU sets RCX=return RIP, R11=RFLAGS, CS/SS from STAR,
; but does NOT switch RSP.  We save the user RSP, switch to a kernel
; stack, reshuffle the syscall args into the SysV C ABI, call the C
; dispatcher, then SYSRET back to ring 3.
;
; Syscall ABI:  RAX=number, args in RDI, RSI, RDX.
; C dispatcher: syscall_dispatch(num, a1, a2, a3) → RDI,RSI,RDX,RCX.
; ─────────────────────────────────────────────────────────────
[BITS 64]
global syscall_entry
extern syscall_dispatch
extern g_kernel_rsp
extern g_user_rsp

syscall_entry:
    mov  [rel g_user_rsp], rsp        ; save user RSP
    mov  rsp, [rel g_kernel_rsp]      ; switch to kernel stack

    push rcx                          ; save user RIP (clobbered by call)
    push r11                          ; save user RFLAGS

    ; Reshuffle: num=rax, a1=rdi, a2=rsi, a3=rdx → C(rdi,rsi,rdx,rcx)
    mov  r8, rdi
    mov  r9, rsi
    mov  r10, rdx
    mov  rdi, rax                     ; arg0 = number
    mov  rsi, r8                      ; arg1
    mov  rdx, r9                      ; arg2
    mov  rcx, r10                     ; arg3

    call syscall_dispatch             ; result in RAX

    pop  r11                          ; restore user RFLAGS
    pop  rcx                          ; restore user RIP
    mov  rsp, [rel g_user_rsp]        ; restore user RSP
    o64 sysret                        ; back to ring 3 (CS/SS from STAR)
