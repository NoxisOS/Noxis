//! Kernel heap — 16 MiB static backing + linked_list_allocator.
//!
//! The static array lives in BSS; the bootloader maps it automatically.
//! After init() Box/Vec/String/BTreeMap are available everywhere in the kernel.

use linked_list_allocator::LockedHeap;

const HEAP_SIZE: usize = 16 * 1024 * 1024; // 16 MiB

#[repr(align(4096))]
struct HeapBacking([u8; HEAP_SIZE]);

static mut HEAP_MEM: HeapBacking = HeapBacking([0u8; HEAP_SIZE]);

#[global_allocator]
pub static ALLOCATOR: LockedHeap = LockedHeap::empty();

/// Initialize the kernel heap. Must be called before any alloc use.
pub fn init() {
    unsafe {
        ALLOCATOR
            .lock()
            .init(HEAP_MEM.0.as_mut_ptr(), HEAP_SIZE);
    }
}

/// Return (used_bytes, free_bytes).
pub fn stats() -> (usize, usize) {
    let h = ALLOCATOR.lock();
    (h.used(), h.free())
}
