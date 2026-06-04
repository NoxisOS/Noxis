//! CPU context — saved registers for kernel-mode context switches.
//!
//! Layout must match the push/pop order in context_switch().

#[derive(Debug, Default, Clone, Copy)]
#[repr(C)]
pub struct KernelContext {
    pub rbx: u64,
    pub rbp: u64,
    pub r12: u64,
    pub r13: u64,
    pub r14: u64,
    pub r15: u64,
    pub rsp: u64, // stack pointer after pushing callee-saved regs
    pub rip: u64, // instruction pointer (for new threads)
}

impl KernelContext {
    pub const fn new() -> Self {
        Self { rbx: 0, rbp: 0, r12: 0, r13: 0, r14: 0, r15: 0, rsp: 0, rip: 0 }
    }
}

/// User-mode register snapshot saved on syscall/interrupt entry.
#[derive(Debug, Default, Clone, Copy)]
#[repr(C)]
pub struct UserRegs {
    // Saved by our entry stubs (push order matters for iretq)
    pub r15: u64, pub r14: u64, pub r13: u64, pub r12: u64,
    pub r11: u64, pub r10: u64, pub r9:  u64, pub r8:  u64,
    pub rbp: u64, pub rdi: u64, pub rsi: u64, pub rdx: u64,
    pub rcx: u64, pub rbx: u64, pub rax: u64,
    // Pushed by CPU on interrupt/syscall
    pub rip: u64, pub cs:  u64, pub rflags: u64,
    pub rsp: u64, pub ss:  u64,
}

/// Switch from `from` to `to` kernel contexts.
/// Saves callee-saved registers, swaps rsp, restores and returns.
///
/// # Safety: both contexts must have valid stacks set up.
#[unsafe(naked)]
pub unsafe extern "C" fn context_switch(
    from: *mut KernelContext,
    to:   *const KernelContext,
) {
    core::arch::naked_asm!(
        // Save callee-saved registers onto current stack
        "push rbx",
        "push rbp",
        "push r12",
        "push r13",
        "push r14",
        "push r15",
        // Save current rsp into from->rsp (field offset 6*8 = 48)
        "mov [rdi + 48], rsp",
        // Load new rsp from to->rsp
        "mov rsp, [rsi + 48]",
        // Restore callee-saved registers from new stack
        "pop r15",
        "pop r14",
        "pop r13",
        "pop r12",
        "pop rbp",
        "pop rbx",
        "ret",
    );
}
