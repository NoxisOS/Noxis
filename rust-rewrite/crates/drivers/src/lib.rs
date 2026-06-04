//! `drivers` — hardware drivers.
//!
//! Each driver implements a standard trait (`Write`, `BlockDevice`, etc.).
//! The kernel only manipulates traits — zero hard coupling.
#![no_std]

pub mod serial;
pub mod vga;

pub use core::fmt::Write as FmtWrite;
