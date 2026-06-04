//! Signal numbers and pending-signal bitmask.

use core::sync::atomic::{AtomicU64, Ordering};

pub const SIGHUP:  u64 = 1;
pub const SIGINT:  u64 = 2;
pub const SIGQUIT: u64 = 3;
pub const SIGKILL: u64 = 9;
pub const SIGTERM: u64 = 15;
pub const SIGCHLD: u64 = 17;
pub const SIGPIPE: u64 = 13;
pub const SIGSTOP: u64 = 19;
pub const SIGCONT: u64 = 18;

/// Atomic pending-signal set (one bit per signal number 1–63).
pub struct SigSet(AtomicU64);

impl SigSet {
    pub const fn new() -> Self { Self(AtomicU64::new(0)) }
    pub fn send(&self, sig: u64)   { self.0.fetch_or(1 << sig, Ordering::Relaxed); }
    pub fn clear(&self, sig: u64)  { self.0.fetch_and(!(1 << sig), Ordering::Relaxed); }
    pub fn pending(&self) -> u64   { self.0.load(Ordering::Relaxed) }
    pub fn is_pending(&self, sig: u64) -> bool { self.pending() & (1 << sig) != 0 }
    pub fn take_next(&self) -> Option<u64> {
        let p = self.pending();
        if p == 0 { return None; }
        let sig = p.trailing_zeros() as u64;
        self.clear(sig);
        Some(sig)
    }
}

/// Default signal disposition.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SigAction {
    Default,
    Ignore,
    // Handler(fn(*mut UserRegs)) — userland handlers TODO
}

pub const NSIG: usize = 64;

pub struct SigHandlers {
    pub actions: [SigAction; NSIG],
}

impl SigHandlers {
    pub fn new() -> Self {
        Self { actions: [SigAction::Default; NSIG] }
    }
}
