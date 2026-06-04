//! `kernel` — Noxis OS entry point.
//!
//! Boot chain:
//!   UEFI firmware → `bootloader` crate → `kernel_main`
//!
//! Initialization order:
//!   HAL (GDT→IDT→PIC) → mm (heap→PMM→VMM) → drivers (serial→PIT→KBD)
//!   → VFS (ramfs→procfs→devfs) → syscall (SYSCALL/SYSRET)
//!   → scheduler → idle loop

#![no_std]
#![no_main]
#![feature(abi_x86_interrupt)]

extern crate alloc;

mod initrd;

use bootloader_api::{entry_point, BootInfo, BootloaderConfig};
use bootloader_api::config::Mapping;
use core::panic::PanicInfo;
use alloc::sync::Arc;

// ── Bootloader config ────────────────────────────────────────────────────────
pub static BOOTLOADER_CONFIG: BootloaderConfig = {
    let mut c = BootloaderConfig::new_default();
    c.mappings.physical_memory = Some(Mapping::Dynamic);
    c
};

entry_point!(kernel_main, config = &BOOTLOADER_CONFIG);

// ── Entry point ──────────────────────────────────────────────────────────────
fn kernel_main(boot_info: &'static mut BootInfo) -> ! {
    // ── P1: HAL ──────────────────────────────────────────────────────────────
    hal::gdt::init();
    hal::idt::init();
    hal::pic::init();

    // Serial online first (everything logs to it)
    kprintln!("Noxis OS — Rust kernel booting...");

    // ── P3: Memory ───────────────────────────────────────────────────────────
    mm::init(boot_info);
    let (free, total) = mm::pmm::stats();
    kprintln!("PMM: {}/{} frames free ({} MiB / {} MiB)",
        free, total, free * 4 / 1024, total * 4 / 1024);

    // ── P5: Drivers ──────────────────────────────────────────────────────────
    drivers::pit::init();
    drivers::kbd::init();
    kprintln!("PIT: {} Hz, KBD: online", drivers::pit::TICK_HZ);

    // Register PIT tick function for sched
    sched::register_pit_ticks(drivers::pit::ticks);

    // ── P7: VFS ──────────────────────────────────────────────────────────────
    kprintln!("VFS: init...");
    init_vfs();
    kprintln!("VFS: done");

    kprintln!("Checking ramdisk...");
    // Load ramdisk: ramdisk_addr is a virtual address mapped by the bootloader.
    if let Some(ramdisk_virt) = boot_info.ramdisk_addr.into_option() {
        let ramdisk_len = boot_info.ramdisk_len as usize;
        kprintln!("ramdisk at virt {:#x}, {} KiB", ramdisk_virt, ramdisk_len / 1024);

        // Sanity-check: read the first 4 bytes to verify the mapping is accessible
        let first_bytes = unsafe {
            let p = ramdisk_virt as *const u32;
            kprintln!("ramdisk first u32 = {:#x}", *p);
            *p
        };

        if ramdisk_len > 0 {
            let ramdisk: &'static [u8] = unsafe {
                core::slice::from_raw_parts(ramdisk_virt as *const u8, ramdisk_len)
            };
            initrd::load(ramdisk);
        }
    } else {
        kprintln!("No ramdisk provided.");
    }
    kprintln!("VFS: ramfs/procfs/devfs mounted");

    kprintln!("P8: syscall init...");
    // ── P8: Syscall ──────────────────────────────────────────────────────────
    syscall::init();
    kprintln!("SYSCALL/SYSRET: online");

    // ── P6: Scheduler ────────────────────────────────────────────────────────
    hal::idt::register_timer(sched::scheduler::tick);
    sched::scheduler::init();
    kprintln!("Scheduler: online");

    // ── Banner ───────────────────────────────────────────────────────────────
    kprintln!("\r\n\x1b[32m╔══════════════════════════════════════════╗");
    kprintln!("║         N O X I S   O S  (Rust)          ║");
    kprintln!("║      All subsystems online — ready        ║");
    kprintln!("╚══════════════════════════════════════════╝\x1b[0m");

    // Enable interrupts and idle
    hal::cpu::sti();

    loop {
        sched::scheduler::wake_sleepers();
        unsafe { core::arch::asm!("hlt", options(nomem, nostack)) };
    }
}

// ── VFS initialization ───────────────────────────────────────────────────────

fn init_vfs() {
    use vfs::vfs::with_vfs;
    use vfs::ramfs::RamFs;
    use vfs::procfs::ProcFs;
    use vfs::devfs::DevFs;

    kprintln!("VFS: mounting ramfs...");
    with_vfs(|vfs| {
        vfs.mount("/",     Arc::new(RamFs::new()));
    });
    kprintln!("VFS: mounting procfs...");
    with_vfs(|vfs| {
        vfs.mount("/proc", Arc::new(ProcFs));
    });
    kprintln!("VFS: mounting devfs...");
    with_vfs(|vfs| {
        vfs.mount("/dev",  Arc::new(DevFs));
    });
    kprintln!("VFS: creating dirs...");
    // Create basic directory structure on root ramfs
    with_vfs(|vfs| {
        let root = vfs.resolve("/").unwrap();
        let _ = root.mkdir("bin",  0o755);
        let _ = root.mkdir("etc",  0o755);
        let _ = root.mkdir("tmp",  0o1777);
        let _ = root.mkdir("home", 0o755);
    });
}

// ── Panic handler ────────────────────────────────────────────────────────────
#[panic_handler]
fn panic(info: &PanicInfo) -> ! {
    hal::cpu::cli();
    use core::fmt::Write;
    if let Some(mut serial) = drivers::serial::COM1_SERIAL.try_lock() {
        let _ = writeln!(serial, "\r\n\x1b[31m[KERNEL PANIC] {}\x1b[0m\r\n", info);
    }
    hal::cpu::halt_loop();
}

// ── kprintln! macro ──────────────────────────────────────────────────────────
#[macro_export]
macro_rules! kprintln {
    ($($arg:tt)*) => {{
        use core::fmt::Write;
        let mut s = drivers::serial::COM1_SERIAL.lock();
        let _ = writeln!(s, $($arg)*);
    }};
}
