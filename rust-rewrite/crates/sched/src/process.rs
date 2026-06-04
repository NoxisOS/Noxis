//! Process descriptor — owns address space, FD table, signals, env.

use alloc::{
    string::String,
    vec::Vec,
    collections::BTreeMap,
    sync::Arc,
    boxed::Box,
};
use sync::Mutex;
use core::sync::atomic::{AtomicU32, Ordering};

use crate::context::KernelContext;
use crate::fd::FdTable;
use crate::signal::{SigSet, SigHandlers};

// ── PID allocator ─────────────────────────────────────────────────────────────

static NEXT_PID: AtomicU32 = AtomicU32::new(1);
pub fn alloc_pid() -> u32 { NEXT_PID.fetch_add(1, Ordering::Relaxed) }

// ── Process state ─────────────────────────────────────────────────────────────

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ProcessState {
    Running,
    Ready,
    Sleeping(u64),  // wake tick
    Zombie(i32),    // exit code
    Stopped,
}

// ── Address space ─────────────────────────────────────────────────────────────

/// Describes the user-space layout for one process.
pub struct AddressSpace {
    /// Physical frame of the top-level PML4.
    pub pml4_phys: u64,
    /// Current program break (heap top).
    pub brk: u64,
    /// Lowest mapped stack page.
    pub stack_low: u64,
}

impl AddressSpace {
    pub fn kernel_only() -> Self {
        // Use the current (kernel) PML4
        let pml4 = unsafe {
            let (frame, _) = x86_64::registers::control::Cr3::read();
            frame.start_address().as_u64()
        };
        Self { pml4_phys: pml4, brk: 0x10_0000, stack_low: 0x4FFF_0000 }
    }
}

// ── Process ───────────────────────────────────────────────────────────────────

/// Kernel stack size (16 KiB per process).
pub const KSTACK_SIZE: usize = 16 * 1024;

pub struct Process {
    pub pid:    u32,
    pub ppid:   u32,
    pub state:  ProcessState,

    // CPU context (kernel-mode execution pointer)
    pub kctx:   KernelContext,

    // Kernel stack — owned by the process
    pub kstack: Vec<u8>,

    // Address space
    pub addr:   AddressSpace,

    // File descriptors
    pub fds:    FdTable,

    // Signals
    pub pending: SigSet,
    pub handlers: SigHandlers,

    // Environment + working directory
    pub env:    BTreeMap<String, String>,
    pub cwd:    String,
    pub name:   String,

    // Exit/wait
    pub exit_code: i32,
}

impl Process {
    pub fn new_kernel(name: &str, entry: fn() -> !) -> Self {
        let pid = alloc_pid();
        let mut kstack = Vec::with_capacity(KSTACK_SIZE);
        kstack.resize(KSTACK_SIZE, 0u8);

        // Set up the initial kernel context so when we switch to this process,
        // it starts at `entry`.
        let stack_top = kstack.as_ptr() as u64 + KSTACK_SIZE as u64;
        let mut kctx = KernelContext::new();
        kctx.rsp = stack_top - 8; // reserve return address slot
        kctx.rip = entry as u64;

        // Write entry address at top of stack (context_switch will ret to it)
        unsafe {
            let ret_addr = (stack_top - 8) as *mut u64;
            *ret_addr = entry as u64;
        }

        Self {
            pid, ppid: 0, state: ProcessState::Ready,
            kctx, kstack,
            addr: AddressSpace::kernel_only(),
            fds: FdTable::new(),
            pending: SigSet::new(),
            handlers: SigHandlers::new(),
            env: BTreeMap::new(),
            cwd: String::from("/"),
            name: String::from(name),
            exit_code: 0,
        }
    }
}

// ── Global process table ──────────────────────────────────────────────────────

pub type ProcTable = BTreeMap<u32, Process>;
static PROCS: Mutex<Option<ProcTable>> = Mutex::new(None);

pub fn with_procs<F, R>(f: F) -> R
where F: FnOnce(&mut ProcTable) -> R {
    let mut guard = PROCS.lock();
    f(guard.get_or_insert_with(ProcTable::new))
}

pub fn add_process(p: Process) -> u32 {
    let pid = p.pid;
    with_procs(|t| { t.insert(pid, p); });
    pid
}
