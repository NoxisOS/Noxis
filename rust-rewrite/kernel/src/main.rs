//! `kernel` — Noxis OS entry point.
//!
//! Boot chain:
//!   UEFI firmware → `bootloader` crate (long mode, paging, mmap) → `kernel_main`
//!
//! Tier 1: GDT + IDT + PIC + serial initialized, banner printed, then hlt loop.

#![no_std]
#![no_main]
#![feature(abi_x86_interrupt)]

use bootloader_api::{entry_point, BootInfo, BootloaderConfig};
use bootloader_api::config::Mapping;
use core::panic::PanicInfo;

// ── Bootloader config ────────────────────────────────────────────────────────
// Request a full physical memory mapping at a dynamic virtual address.
pub static BOOTLOADER_CONFIG: BootloaderConfig = {
    let mut c = BootloaderConfig::new_default();
    c.mappings.physical_memory = Some(Mapping::Dynamic);
    c
};

entry_point!(kernel_main, config = &BOOTLOADER_CONFIG);

// ── Entry point ──────────────────────────────────────────────────────────────
fn kernel_main(boot_info: &'static mut BootInfo) -> ! {
    hal::gdt::init();
    hal::idt::init();
    hal::pic::init();

    use core::fmt::Write;
    {
        let mut serial = drivers::serial::COM1_SERIAL.lock();
        let _ = writeln!(serial, "\r\n\
\x1b[32m╔══════════════════════════════════════════╗\r\n\
║         N O X I S   O S  (Rust)          ║\r\n\
║         Tier 1 — UEFI boot online        ║\r\n\
╚══════════════════════════════════════════╝\x1b[0m\r\n");
    }

    log_memory_map(boot_info);
    hal::cpu::sti();

    {
        let mut serial = drivers::serial::COM1_SERIAL.lock();
        let _ = writeln!(serial, "IRQs enabled — kernel idle (hlt loop).");
    }

    hal::cpu::halt_loop();
}

fn log_memory_map(boot_info: &BootInfo) {
    use core::fmt::Write;
    use bootloader_api::info::MemoryRegionKind;

    let mut serial = drivers::serial::COM1_SERIAL.lock();
    let _ = writeln!(serial, "Memory map ({} regions):", boot_info.memory_regions.len());

    let mut total_usable: u64 = 0;
    for region in boot_info.memory_regions.iter() {
        let kind_str = match region.kind {
            MemoryRegionKind::Usable     => "Usable    ",
            MemoryRegionKind::Bootloader => "Bootloader",
            MemoryRegionKind::UnknownBios(_)  => "BIOS      ",
            MemoryRegionKind::UnknownUefi(_)  => "UEFI      ",
            _                            => "Other     ",
        };
        let _ = writeln!(
            serial,
            "  [{:#012x} – {:#012x}] {}",
            region.start, region.end, kind_str
        );
        if region.kind == MemoryRegionKind::Usable {
            total_usable += region.end - region.start;
        }
    }
    let _ = writeln!(serial, "  Total usable: {} MiB", total_usable >> 20);
}

// ── Panic handler ────────────────────────────────────────────────────────────
#[panic_handler]
fn panic(info: &PanicInfo) -> ! {
    use core::fmt::Write;
    if let Some(mut serial) = drivers::serial::COM1_SERIAL.try_lock() {
        let _ = writeln!(serial, "\r\n\x1b[31m[KERNEL PANIC] {}\x1b[0m\r\n", info);
    }
    hal::cpu::halt_loop();
}
