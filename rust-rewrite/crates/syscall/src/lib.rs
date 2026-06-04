//! `syscall` — SYSCALL/SYSRET entry + dispatcher.
//!
//! Setup: wrmsr(LSTAR, syscall_entry); wrmsr(STAR, ...); wrmsr(SFMASK, ...)
//! ABI (SysV): num=rax, args=rdi/rsi/rdx/r10/r8/r9, return in rax.
#![no_std]
#![feature(naked_functions, abi_x86_interrupt)]

extern crate alloc;

pub mod entry;
pub mod handlers;

use hal::cpu::{wrmsr, MSR_STAR, MSR_LSTAR, MSR_SFMASK, MSR_EFER};
use hal::gdt;

/// Initialize SYSCALL/SYSRET MSRs.
pub fn init() {
    let sel = gdt::selectors();

    // STAR: [63:48]=user cs-8 (sysret uses cs+8,ss+16), [47:32]=kernel cs
    let star: u64 = ((sel.user_code.0 as u64 - 8) << 48)
                  | ((sel.kernel_code.0 as u64) << 32);

    // LSTAR: syscall entry point
    let lstar = entry::syscall_entry as u64;

    // SFMASK: mask RFLAGS bits on syscall entry (clear IF = disable IRQs)
    let sfmask: u64 = 1 << 9; // IF bit

    unsafe {
        // Enable SCE (syscall extensions) in EFER
        let efer = rdmsr(MSR_EFER);
        wrmsr(MSR_EFER, efer | 1);
        wrmsr(MSR_STAR,   star);
        wrmsr(MSR_LSTAR,  lstar);
        wrmsr(MSR_SFMASK, sfmask);
    }
}

use hal::cpu::rdmsr;
