//! Round-robin preemptive scheduler.
//!
//! The timer tick (IRQ0) calls tick(), which checks if the running process
//! has used its quantum and calls schedule() if so.

use alloc::collections::VecDeque;
use sync::Mutex;
use core::sync::atomic::{AtomicU32, AtomicBool, Ordering};

use crate::process::{ProcessState, with_procs, KSTACK_SIZE};
use crate::context::{KernelContext, context_switch};

pub const QUANTUM_TICKS: u64 = 10; // 10 ticks @ 100 Hz = 100 ms

static CURRENT_PID: AtomicU32   = AtomicU32::new(0);
static TICK_COUNT:  AtomicU32   = AtomicU32::new(0);
static INITIALIZED: AtomicBool  = AtomicBool::new(false);

/// Ready queue — PIDs in round-robin order.
static RUN_QUEUE: Mutex<VecDeque<u32>> = Mutex::new(VecDeque::new());

/// Initialize the scheduler (called after first process is created).
pub fn init() {
    INITIALIZED.store(true, Ordering::Release);
}

/// Enqueue a PID into the run queue.
pub fn enqueue(pid: u32) {
    RUN_QUEUE.lock().push_back(pid);
}

/// Called by IRQ0 (PIT tick). Decrements quantum and preempts if expired.
pub fn tick() {
    if !INITIALIZED.load(Ordering::Acquire) { return; }
    let t = TICK_COUNT.fetch_add(1, Ordering::Relaxed);
    if t % QUANTUM_TICKS as u32 == 0 {
        schedule();
    }
}

/// Pick the next ready process and switch to it.
pub fn schedule() {
    let current = CURRENT_PID.load(Ordering::Relaxed);

    // Put current back at end of queue (if still running)
    let next = {
        let mut q = RUN_QUEUE.lock();
        // Re-enqueue current if it's still runnable
        if current != 0 {
            let still_running = with_procs(|t| {
                t.get(&current).map(|p| p.state == ProcessState::Running).unwrap_or(false)
            });
            if still_running { q.push_back(current); }
        }
        // Pick next
        loop {
            let Some(pid) = q.pop_front() else { return; };
            let ready = with_procs(|t| {
                t.get(&pid).map(|p| p.state == ProcessState::Ready || p.state == ProcessState::Running).unwrap_or(false)
            });
            if ready { break pid; }
        }
    };

    if next == current { return; }

    CURRENT_PID.store(next, Ordering::Relaxed);

    // Get the two context pointers
    with_procs(|t| {
        // Mark old process as Ready
        if let Some(p) = t.get_mut(&current) {
            if p.state == ProcessState::Running {
                p.state = ProcessState::Ready;
            }
        }
        // Mark new process as Running
        if let Some(p) = t.get_mut(&next) {
            p.state = ProcessState::Running;
        }
    });

    // Perform the actual context switch
    // SAFETY: both processes exist and have valid kernel stacks
    with_procs(|t| {
        let from_ctx = t.get_mut(&current).map(|p| &mut p.kctx as *mut KernelContext);
        let to_ctx   = t.get(&next).map(|p| &p.kctx as *const KernelContext);
        if let (Some(from), Some(to)) = (from_ctx, to_ctx) {
            unsafe { context_switch(from, to); }
        }
    });
}

/// Block the current process for `ms` milliseconds.
pub fn sleep_ms(ms: u64) {
    use crate::pit_ticks;
    let wake_tick = pit_ticks() + ms * 100 / 1000 + 1;
    let pid = CURRENT_PID.load(Ordering::Relaxed);
    with_procs(|t| {
        if let Some(p) = t.get_mut(&pid) {
            p.state = ProcessState::Sleeping(wake_tick);
        }
    });
    schedule();
}

/// Wake sleeping processes whose tick has come.
pub fn wake_sleepers() {
    let now = crate::pit_ticks();
    let mut to_wake = alloc::vec::Vec::new();
    with_procs(|t| {
        for (pid, p) in t.iter_mut() {
            if let ProcessState::Sleeping(wake) = p.state {
                if now >= wake {
                    p.state = ProcessState::Ready;
                    to_wake.push(*pid);
                }
            }
        }
    });
    for pid in to_wake { enqueue(pid); }
}

pub fn current_pid() -> u32 { CURRENT_PID.load(Ordering::Relaxed) }
