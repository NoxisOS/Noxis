//! Userland heap — sbrk-based, linked_list_allocator style.
//!
//! Uses sys_brk to grow the heap on demand.

use core::alloc::{GlobalAlloc, Layout};
use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicUsize, Ordering};

// Simple bump allocator with free-list for userland.
// For now: bump only (sufficient for simple programs).
struct BumpAlloc {
    start: AtomicUsize,
    end:   AtomicUsize,
    cur:   AtomicUsize,
}

impl BumpAlloc {
    const fn new() -> Self {
        Self {
            start: AtomicUsize::new(0),
            end:   AtomicUsize::new(0),
            cur:   AtomicUsize::new(0),
        }
    }

    fn init(&self) {
        // Ask kernel for current brk
        let brk = crate::sys_brk(0);
        self.start.store(brk, Ordering::Relaxed);
        self.cur.store(brk, Ordering::Relaxed);
        self.end.store(brk, Ordering::Relaxed);
    }

    fn grow(&self, by: usize) -> bool {
        let new_end = self.end.load(Ordering::Relaxed) + by;
        let actual = crate::sys_brk(new_end);
        if actual >= new_end {
            self.end.store(actual, Ordering::Relaxed);
            true
        } else {
            false
        }
    }
}

unsafe impl GlobalAlloc for BumpAlloc {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        // Ensure initialized
        if self.start.load(Ordering::Relaxed) == 0 {
            self.init();
        }
        let align = layout.align();
        let size  = layout.size();

        loop {
            let cur = self.cur.load(Ordering::Acquire);
            let aligned = (cur + align - 1) & !(align - 1);
            let new_cur = aligned + size;

            if new_cur > self.end.load(Ordering::Relaxed) {
                // Grow by at least 64 KiB or what we need
                let grow_by = (new_cur - self.end.load(Ordering::Relaxed) + 0xFFFF) & !0xFFFF;
                if !self.grow(grow_by) {
                    return core::ptr::null_mut();
                }
            }

            match self.cur.compare_exchange(cur, new_cur, Ordering::AcqRel, Ordering::Acquire) {
                Ok(_) => return aligned as *mut u8,
                Err(_) => continue,
            }
        }
    }

    unsafe fn dealloc(&self, _ptr: *mut u8, _layout: Layout) {
        // Bump allocator: no-op dealloc (memory leak — acceptable for short-lived programs)
    }
}

#[global_allocator]
pub static ALLOCATOR: BumpAlloc = BumpAlloc::new();
