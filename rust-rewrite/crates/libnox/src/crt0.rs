//! crt0 — userland entry point.
//!
//! The kernel jumps here (rdi=argc, rsi=argv ptr).
//! We call main() then sys_exit().

#[unsafe(naked)]
#[no_mangle]
pub unsafe extern "C" fn _start() {
    core::arch::naked_asm!(
        // rdi = argc, rsi = argv (passed by kernel exec)
        "call main",
        "mov rdi, rax",  // exit code = main return value
        "mov rax, 6",    // SYS_EXIT
        "syscall",
        "ud2",
    );
}
