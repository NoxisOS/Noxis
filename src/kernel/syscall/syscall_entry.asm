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

    ; The syscall ABI preserves all regs except RAX/RCX/R11, so save the
    ; user's RDI/RSI/RDX/R8/R9/R10 and restore them before returning.
    push rdi
    push rsi
    push rdx
    push r8
    push r9
    push r10

    ; Reshuffle: num=rax, a1=rdi, a2=rsi, a3=rdx → C(rdi,rsi,rdx,rcx).
    ; Done in an order that never overwrites a value before it is used.
    mov  rcx, rdx                     ; arg3 = a3
    mov  rdx, rsi                     ; arg2 = a2
    mov  rsi, rdi                     ; arg1 = a1
    mov  rdi, rax                     ; arg0 = number

    call syscall_dispatch             ; result in RAX (returned to user)

    pop  r10
    pop  r9
    pop  r8
    pop  rdx
    pop  rsi
    pop  rdi
    pop  r11                          ; restore user RFLAGS
    pop  rcx                          ; restore user RIP
    mov  rsp, [rel g_user_rsp]        ; restore user RSP
    o64 sysret                        ; back to ring 3 (CS/SS from STAR)
