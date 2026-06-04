//! `hal` — Hardware Abstraction Layer for x86-64.
//!
//! All low-level `unsafe` lives here: GDT, IDT, PIC, I/O ports, MSRs.
//! The rest of the kernel never needs to write `unsafe` to access hardware.
#![no_std]
#![feature(abi_x86_interrupt)]

pub mod port;
pub mod gdt;
pub mod idt;
pub mod pic;
pub mod cpu;
