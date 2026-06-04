//! SYSCALL entry stub — saves user registers, calls dispatcher, SYSRET.

use crate::handlers::dispatch;

/// SYSCALL entry point.
/// On entry: rax=num, rdi/rsi/rdx/r10/r8/r9=args,
///           rcx=user rip, r11=user rflags.
#[unsafe(naked)]
pub unsafe extern "C" fn syscall_entry() {
    core::arch::naked_asm!(
        // Switch to kernel stack (TODO: load from per-CPU TSS gs)
        // For now we stay on whatever stack user had (single process only)

        // Save user registers
        "push rax",
        "push rbx",
        "push rcx",   // user rip
        "push rdx",
        "push rsi",
        "push rdi",
        "push rbp",
        "push r8",
        "push r9",
        "push r10",
        "push r11",   // user rflags
        "push r12",
        "push r13",
        "push r14",
        "push r15",

        // Move r10 to rcx for 4th arg (SysV uses rcx but SYSCALL clobbers it)
        "mov rcx, r10",

        // Call Rust dispatcher: dispatch(num, a0, a1, a2, a3, a4, a5)
        // rax=num already, rdi/rsi/rdx/rcx(=r10)/r8/r9 already set
        "call {dispatch}",

        // Return value in rax (already there from Rust return)

        // Restore user registers
        "pop r15",
        "pop r14",
        "pop r13",
        "pop r12",
        "pop r11",    // user rflags → r11 for sysret
        "pop r10",
        "pop r9",
        "pop r8",
        "pop rbp",
        "pop rdi",
        "pop rsi",
        "pop rdx",
        "pop rcx",    // user rip → rcx for sysret
        "pop rbx",
        "add rsp, 8", // skip original rax slot

        // Re-enable interrupts and return to user
        "sti",
        "sysretq",
        dispatch = sym dispatch,
    );
}
