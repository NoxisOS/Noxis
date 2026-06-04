//! `sched` — processes, threads, round-robin scheduler.
#![no_std]
#![feature(naked_functions)]

extern crate alloc;

pub mod process;
pub mod scheduler;
pub mod context;
pub mod elf;
pub mod fd;
pub mod signal;

use core::sync::atomic::{AtomicU64, Ordering};

/// Set by kernel_main after PIT is initialized.
static PIT_TICKS_FN: AtomicU64 = AtomicU64::new(0);

pub fn register_pit_ticks(f: fn() -> u64) {
    PIT_TICKS_FN.store(f as u64, Ordering::Relaxed);
}

pub fn pit_ticks() -> u64 {
    let f = PIT_TICKS_FN.load(Ordering::Relaxed);
    if f != 0 {
        let f: fn() -> u64 = unsafe { core::mem::transmute(f) };
        f()
    } else {
        0
    }
}
