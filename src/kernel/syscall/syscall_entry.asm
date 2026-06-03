; ─────────────────────────────────────────────────────────────
; kernel/syscall/syscall_entry.asm — SYSCALL instruction entry (x86-64).
;
; On SYSCALL the CPU sets RCX=return RIP, R11=RFLAGS, CS/SS from STAR, but
; does NOT switch RSP.  We save the user RSP, switch to the *current process's*
; kernel stack (g_cur_kstack, also TSS.rsp0), build a full syscall_frame_t so
; fork() can clone the caller's context, call the C dispatcher, then SYSRET
; back to ring 3.
;
; Syscall ABI:  RAX=number, args in RDI, RSI, RDX.
; syscall_frame_t field order (low→high addr) = push order below, reversed.
; ─────────────────────────────────────────────────────────────
[BITS 64]
global syscall_entry
global fork_ret_trampoline
extern syscall_dispatch
extern g_cur_kstack
extern g_user_rsp

syscall_entry:
    mov  [rel g_user_rsp], rsp        ; stash user RSP (scratch global)
    mov  rsp, [rel g_cur_kstack]      ; switch to this process's kernel stack

    ; Build syscall_frame_t (rax pushed last → lowest address → first field).
    push qword [rel g_user_rsp]       ; ursp
    push r11                          ; rflags
    push rcx                          ; rip
    push r15
    push r14
    push r13
    push r12
    push rbp
    push rbx
    push r9
    push r8
    push r10
    push rdx
    push rsi
    push rdi
    push rax

    mov  rdi, rsp                     ; arg0 = syscall_frame_t*
    call syscall_dispatch             ; dispatcher writes the result to f->rax

    ; ── Shared ring-3 return path (fork child re-enters here) ──
.return:
    pop  rax
    pop  rdi
    pop  rsi
    pop  rdx
    pop  r10
    pop  r8
    pop  r9
    pop  rbx
    pop  rbp
    pop  r12
    pop  r13
    pop  r14
    pop  r15
    pop  rcx                          ; user RIP
    pop  r11                          ; user RFLAGS
    mov  rsp, [rsp]                   ; RSP slot → user RSP
    o64 sysret                        ; back to ring 3 (CS/SS from STAR)

; A forked child starts here: the scheduler switched CR3 + kernel stack to the
; child, kthread_switch `ret`d here with RSP pointing at the cloned frame.
fork_ret_trampoline:
    jmp  syscall_entry.return
