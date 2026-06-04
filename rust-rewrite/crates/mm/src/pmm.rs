//! Physical Memory Manager — bitmap frame allocator.
//!
//! Supports up to 64 GiB of RAM (16 M frames × 4 KiB).
//! Bitmap is stored statically in BSS (2 MiB).
//! Free frame = bit 0, used frame = bit 1.

use bootloader_api::info::{MemoryRegions, MemoryRegionKind};
use x86_64::PhysAddr;
use sync::Mutex;

pub const FRAME_SIZE: usize = 4096;
const MAX_FRAMES: usize = (64 * 1024 * 1024 * 1024) / FRAME_SIZE; // 16 M frames for 64 GiB
const BITMAP_BYTES: usize = MAX_FRAMES / 8;                         // 2 MiB

/// Raw bitmap: bit=0 → free, bit=1 → used.
static mut BITMAP: [u8; BITMAP_BYTES] = [0xFF; BITMAP_BYTES]; // all used by default
static mut TOTAL_FRAMES: usize = 0;
static mut FREE_FRAMES: usize  = 0;

/// Global frame allocator — protected by a spinlock.
static ALLOCATOR: Mutex<BitmapAllocator> = Mutex::new(BitmapAllocator);

struct BitmapAllocator;

impl BitmapAllocator {
    #[inline]
    fn set_used(frame: usize) {
        unsafe { BITMAP[frame / 8] |= 1 << (frame % 8) };
    }
    #[inline]
    fn set_free(frame: usize) {
        unsafe { BITMAP[frame / 8] &= !(1 << (frame % 8)) };
    }
    #[inline]
    fn is_free(frame: usize) -> bool {
        unsafe { BITMAP[frame / 8] & (1 << (frame % 8)) == 0 }
    }

    fn alloc(&self) -> Option<PhysAddr> {
        let total = unsafe { TOTAL_FRAMES };
        for i in 0..total {
            if Self::is_free(i) {
                Self::set_used(i);
                unsafe { FREE_FRAMES = FREE_FRAMES.saturating_sub(1) };
                return Some(PhysAddr::new((i * FRAME_SIZE) as u64));
            }
        }
        None
    }

    fn free(&self, addr: PhysAddr) {
        let frame = addr.as_u64() as usize / FRAME_SIZE;
        let total = unsafe { TOTAL_FRAMES };
        if frame < total && !Self::is_free(frame) {
            Self::set_free(frame);
            unsafe { FREE_FRAMES += 1 };
        }
    }

    fn alloc_contiguous(&self, count: usize) -> Option<PhysAddr> {
        let total = unsafe { TOTAL_FRAMES };
        let mut run = 0;
        let mut start = 0;
        for i in 0..total {
            if Self::is_free(i) {
                if run == 0 { start = i; }
                run += 1;
                if run == count {
                    for j in start..start + count { Self::set_used(j); }
                    unsafe { FREE_FRAMES = FREE_FRAMES.saturating_sub(count) };
                    return Some(PhysAddr::new((start * FRAME_SIZE) as u64));
                }
            } else {
                run = 0;
            }
        }
        None
    }
}

/// Initialize the PMM from the UEFI memory map.
pub fn init(regions: &MemoryRegions, _phys_offset: u64) {
    // Mark all usable regions as free
    for region in regions.iter() {
        if region.kind != MemoryRegionKind::Usable { continue; }

        let start_frame = region.start as usize / FRAME_SIZE;
        let end_frame   = region.end   as usize / FRAME_SIZE;
        let capped_end  = end_frame.min(MAX_FRAMES);

        for frame in start_frame..capped_end {
            unsafe {
                BITMAP[frame / 8] &= !(1 << (frame % 8));
                FREE_FRAMES += 1;
            }
        }

        // Update total frame count
        if capped_end > unsafe { TOTAL_FRAMES } {
            unsafe { TOTAL_FRAMES = capped_end };
        }
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

/// Allocate one 4 KiB physical frame.
pub fn alloc_frame() -> Option<PhysAddr> {
    ALLOCATOR.lock().alloc()
}

/// Allocate `count` contiguous 4 KiB frames.
pub fn alloc_frames(count: usize) -> Option<PhysAddr> {
    ALLOCATOR.lock().alloc_contiguous(count)
}

/// Free a previously allocated frame.
pub fn free_frame(addr: PhysAddr) {
    ALLOCATOR.lock().free(addr);
}

/// Return (free_frames, total_frames).
pub fn stats() -> (usize, usize) {
    unsafe { (FREE_FRAMES, TOTAL_FRAMES) }
}
