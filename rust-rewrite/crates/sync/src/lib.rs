//! `sync` — kernel synchronization primitives.
//!
//! Re-exports `spin` with ergonomic wrappers, plus `IrqLock` which
//! disables interrupts for the duration of the critical section.
#![no_std]

pub use spin::{Mutex, MutexGuard, RwLock, RwLockReadGuard, RwLockWriteGuard, Once, Lazy};

use core::cell::UnsafeCell;
use core::ops::{Deref, DerefMut};

/// A mutex that disables IRQs while held.
/// Use this for data shared between normal code and ISRs.
pub struct IrqLock<T> {
    inner: Mutex<T>,
}

pub struct IrqGuard<'a, T> {
    guard:   MutexGuard<'a, T>,
    _rflags: u64,
}

impl<T> IrqLock<T> {
    pub const fn new(val: T) -> Self {
        Self { inner: Mutex::new(val) }
    }

    pub fn lock(&self) -> IrqGuard<'_, T> {
        let rflags = unsafe { disable_irq() };
        IrqGuard {
            guard: self.inner.lock(),
            _rflags: rflags,
        }
    }
}

impl<T> Deref for IrqGuard<'_, T> {
    type Target = T;
    fn deref(&self) -> &T { &self.guard }
}
impl<T> DerefMut for IrqGuard<'_, T> {
    fn deref_mut(&mut self) -> &mut T { &mut self.guard }
}
impl<T> Drop for IrqGuard<'_, T> {
    fn drop(&mut self) {
        // MutexGuard drops first (automatically), then we restore IRQs
        unsafe { restore_irq(self._rflags) };
    }
}

/// Disable IRQs and return the original RFLAGS.
/// # Safety: low-level CPU instruction, must be paired with restore_irq.
#[inline]
unsafe fn disable_irq() -> u64 {
    let rflags: u64;
    core::arch::asm!(
        "pushfq",
        "pop {0}",
        "cli",
        out(reg) rflags,
        options(nomem, preserves_flags)
    );
    rflags
}

/// Restore RFLAGS (and thus the IRQ enable state).
#[inline]
unsafe fn restore_irq(rflags: u64) {
    if rflags & (1 << 9) != 0 {
        core::arch::asm!("sti", options(nomem, nostack));
    }
}

/// Mutable static cell — UNSAFE: only use in single-threaded context
/// or when already protected by an outer lock.
pub struct UnsafeStaticCell<T>(UnsafeCell<T>);
unsafe impl<T: Send> Sync for UnsafeStaticCell<T> {}
impl<T> UnsafeStaticCell<T> {
    pub const fn new(v: T) -> Self { Self(UnsafeCell::new(v)) }
    /// # Safety
    pub unsafe fn get(&self) -> &mut T { &mut *self.0.get() }
}
