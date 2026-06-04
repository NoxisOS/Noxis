//! PMM — physical frame allocator (bitmap-based).
//!
//! Initialized from the memory map provided by the UEFI bootloader.

use x86_64::PhysAddr;

/// Frame allocator trait — allows swapping implementations.
pub trait FrameAllocator {
    /// Allocate a 4 KiB physical frame. Returns None on OOM.
    fn alloc_frame(&mut self) -> Option<PhysAddr>;
    /// Free a physical frame.
    fn free_frame(&mut self, addr: PhysAddr);
}

// TODO P3: BitmapAllocator driven by UEFI memory map
pub struct BitmapAllocator;

impl FrameAllocator for BitmapAllocator {
    fn alloc_frame(&mut self) -> Option<PhysAddr> { None }
    fn free_frame(&mut self, _addr: PhysAddr) {}
}
