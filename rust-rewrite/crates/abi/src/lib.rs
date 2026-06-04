//! `abi` — shared types between kernel and userland.
//!
//! No `std`, no `alloc`. Only `repr(C)` stable types
//! that can be passed across the syscall boundary.
#![no_std]

pub mod syscall;
pub mod stat;
pub mod error;
pub mod dirent;
