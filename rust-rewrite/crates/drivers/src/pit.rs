//! PIT 8253/8254 — Programmable Interval Timer.
//!
//! Configured to fire IRQ0 at TICK_HZ Hz.
//! A global tick counter is incremented each IRQ; sleep and uptime use it.

use hal::port::Port;
use core::sync::atomic::{AtomicU64, Ordering};

pub const TICK_HZ: u64 = 100; // 100 Hz → 10 ms per tick
const PIT_BASE_FREQ: u64 = 1_193_182;

const PIT_CH0:  u16 = 0x40;
const PIT_CMD:  u16 = 0x43;

static TICKS: AtomicU64 = AtomicU64::new(0);

/// Configure PIT channel 0 to fire at TICK_HZ.
pub fn init() {
    let divisor = (PIT_BASE_FREQ / TICK_HZ) as u16;
    unsafe {
        // Mode 3 (square wave), channel 0, lo/hi access
        Port::<u8>::new(PIT_CMD).write(0x36);
        Port::<u8>::new(PIT_CH0).write((divisor & 0xFF) as u8);
        Port::<u8>::new(PIT_CH0).write((divisor >> 8) as u8);
    }
    // Register the timer IRQ handler in the IDT
    hal::idt::register_timer(on_tick);
    hal::pic::unmask(0);
}

/// Called by IRQ0 handler each tick.
fn on_tick() {
    TICKS.fetch_add(1, Ordering::Relaxed);
}

/// Return elapsed ticks since boot.
#[inline]
pub fn ticks() -> u64 { TICKS.load(Ordering::Relaxed) }

/// Return elapsed milliseconds since boot.
#[inline]
pub fn uptime_ms() -> u64 { ticks() * (1000 / TICK_HZ) }

/// Busy-wait for approximately `ms` milliseconds.
pub fn sleep_ms(ms: u64) {
    let end = ticks() + ms * TICK_HZ / 1000 + 1;
    while ticks() < end {
        unsafe { core::arch::asm!("hlt", options(nomem, nostack)) };
    }
}
