//! `mm` — memory management: PMM, VMM, kernel heap.
//!
//! Initialization order (called from kernel_main):
//!   1. heap::init()  — static heap, enables Box/Vec/String
//!   2. pmm::init()   — bitmap over usable physical frames
//!   3. vmm::init()   — OffsetPageTable for user-space mapping
#![no_std]
#![feature(const_mut_refs)]

extern crate alloc;

pub mod pmm;
pub mod vmm;
pub mod heap;

use bootloader_api::BootInfo;

/// Initialize all memory subsystems. Call once from kernel_main.
pub fn init(boot_info: &'static BootInfo) {
    // Step 1: heap first — needed by pmm bitmap if it were dynamic
    heap::init();

    // Step 2: physical frame allocator
    let phys_offset = boot_info
        .physical_memory_offset
        .into_option()
        .expect("physical_memory_offset not provided by bootloader");

    pmm::init(&boot_info.memory_regions, phys_offset);

    // Step 3: virtual memory mapper
    vmm::init(phys_offset);
}
