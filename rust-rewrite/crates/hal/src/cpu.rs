//! CPU utility instructions.

/// Halt and loop forever (end-of-panic fallback).
#[inline]
pub fn halt_loop() -> ! {
    loop {
        unsafe { core::arch::asm!("hlt", options(nomem, nostack)) };
    }
}

/// Enable interrupts.
#[inline]
pub fn sti() {
    unsafe { core::arch::asm!("sti", options(nomem, nostack)) };
}

/// Disable interrupts.
#[inline]
pub fn cli() {
    unsafe { core::arch::asm!("cli", options(nomem, nostack)) };
}

/// Read a Model Specific Register.
/// # Safety
#[inline]
pub unsafe fn rdmsr(msr: u32) -> u64 {
    let lo: u32;
    let hi: u32;
    core::arch::asm!(
        "rdmsr",
        in("ecx") msr,
        out("eax") lo,
        out("edx") hi,
        options(nomem, nostack)
    );
    ((hi as u64) << 32) | lo as u64
}

/// Write a Model Specific Register.
/// # Safety
#[inline]
pub unsafe fn wrmsr(msr: u32, val: u64) {
    let lo = val as u32;
    let hi = (val >> 32) as u32;
    core::arch::asm!(
        "wrmsr",
        in("ecx") msr,
        in("eax") lo,
        in("edx") hi,
        options(nomem, nostack)
    );
}

pub const MSR_EFER:       u32 = 0xC000_0080;
pub const MSR_STAR:       u32 = 0xC000_0081;
pub const MSR_LSTAR:      u32 = 0xC000_0082;
pub const MSR_SFMASK:     u32 = 0xC000_0084;
pub const MSR_GSBASE:     u32 = 0xC000_0101;
pub const MSR_KERNGSBASE: u32 = 0xC000_0102;
