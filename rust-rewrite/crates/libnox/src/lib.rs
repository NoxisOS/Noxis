//! `libnox` — Noxis userland runtime.
//!
//! no_std, no libc. Provides:
//!   - Syscall wrappers (inline asm SYSCALL)
//!   - Heap (dlmalloc-style bump + free list)
//!   - fmt (write!/println! to stdout)
//!   - String utilities
//!   - Termios / readline
#![no_std]
#![feature(lang_items)]
#![allow(dead_code)]

extern crate alloc;

// Panic handler for userland programs
use core::panic::PanicInfo;
#[panic_handler]
fn panic(info: &PanicInfo) -> ! {
    // Try to write panic message to stderr (fd 2)
    let msg = b"[panic]\n";
    unsafe { raw_write(2, msg.as_ptr(), msg.len()); }
    sys_exit(1);
}

#[lang = "eh_personality"]
fn eh_personality() {}

pub mod syscall;
pub mod io;
pub mod string;
pub mod fmt;
pub mod heap;
pub mod termios;
pub mod readline;
pub mod env;
pub mod process;
pub mod fs;

// Re-export everything at crate root for convenience
pub use syscall::*;
pub use io::*;
pub use string::*;
pub use fmt::*;
pub use env::*;
pub use process::*;
pub use fs::*;
pub use heap::ALLOCATOR;
