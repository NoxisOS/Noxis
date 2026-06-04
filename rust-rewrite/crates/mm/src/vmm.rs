//! Virtual Memory Manager — x86-64 4-level page tables.
//!
//! Wraps x86_64::OffsetPageTable for kernel and user-space mapping.
//! The physical memory offset (physmap) is provided by the UEFI bootloader.

use x86_64::{
    PhysAddr, VirtAddr,
    structures::paging::{
        OffsetPageTable, PageTable, Page, PhysFrame,
        PageTableFlags, Mapper, Size4KiB, FrameAllocator as X64FrameAllocator,
        mapper::MapToError,
    },
};
use sync::Mutex;
use core::ptr::NonNull;

/// Global physical memory offset (physmap base set by bootloader).
static mut PHYS_OFFSET: u64 = 0;

/// Convert a physical address to a kernel virtual address via physmap.
#[inline]
pub fn phys_to_virt(phys: PhysAddr) -> VirtAddr {
    VirtAddr::new(unsafe { PHYS_OFFSET } + phys.as_u64())
}

/// Convert a kernel virtual address (in physmap) to physical.
#[inline]
pub fn virt_to_phys(virt: VirtAddr) -> PhysAddr {
    PhysAddr::new(virt.as_u64() - unsafe { PHYS_OFFSET })
}

/// Initialize VMM with the physmap offset from the bootloader.
pub fn init(phys_offset: u64) {
    unsafe { PHYS_OFFSET = phys_offset; }
}

// ── FrameAllocator bridge ─────────────────────────────────────────────────────

/// Adapts our PMM to the x86_64 FrameAllocator trait.
pub struct PmmBridge;

unsafe impl X64FrameAllocator<Size4KiB> for PmmBridge {
    fn allocate_frame(&mut self) -> Option<PhysFrame<Size4KiB>> {
        let phys = super::pmm::alloc_frame()?;
        Some(PhysFrame::containing_address(phys))
    }
}

// ── Page table access ─────────────────────────────────────────────────────────

/// Get a mutable reference to the active (current CR3) page table.
/// # Safety: caller must ensure the page table is valid.
pub unsafe fn active_page_table() -> &'static mut PageTable {
    use x86_64::registers::control::Cr3;
    let (frame, _) = Cr3::read();
    let phys = frame.start_address();
    let virt = phys_to_virt(phys);
    &mut *virt.as_mut_ptr::<PageTable>()
}

/// Get an OffsetPageTable for the active page table.
/// # Safety: phys_offset must be the correct physmap base.
pub unsafe fn offset_page_table() -> OffsetPageTable<'static> {
    let pt = active_page_table();
    OffsetPageTable::new(pt, VirtAddr::new(PHYS_OFFSET))
}

// ── Mapping helpers ───────────────────────────────────────────────────────────

bitflags::bitflags! {
    /// Page mapping flags (mirrors x86_64 PageTableFlags for our API).
    #[derive(Debug, Clone, Copy)]
    pub struct MapFlags: u64 {
        const PRESENT    = 1 << 0;
        const WRITABLE   = 1 << 1;
        const USER       = 1 << 2;
        const NO_EXECUTE = 1 << 63;
    }
}

impl From<MapFlags> for PageTableFlags {
    fn from(f: MapFlags) -> Self {
        let mut out = PageTableFlags::empty();
        if f.contains(MapFlags::PRESENT)    { out |= PageTableFlags::PRESENT; }
        if f.contains(MapFlags::WRITABLE)   { out |= PageTableFlags::WRITABLE; }
        if f.contains(MapFlags::USER)       { out |= PageTableFlags::USER_ACCESSIBLE; }
        if f.contains(MapFlags::NO_EXECUTE) { out |= PageTableFlags::NO_EXECUTE; }
        out
    }
}

/// Map a single 4 KiB page. Allocates a frame from PMM if phys is None.
pub fn map_page(
    virt: VirtAddr,
    phys: Option<PhysAddr>,
    flags: MapFlags,
) -> Result<PhysAddr, &'static str> {
    let phys_addr = match phys {
        Some(p) => p,
        None    => super::pmm::alloc_frame().ok_or("PMM: out of frames")?,
    };

    let page  = Page::<Size4KiB>::containing_address(virt);
    let frame = PhysFrame::containing_address(phys_addr);
    let pt_flags = PageTableFlags::from(flags);

    unsafe {
        let mut mapper = offset_page_table();
        let mut bridge = PmmBridge;
        mapper
            .map_to(page, frame, pt_flags, &mut bridge)
            .map_err(|_| "map_to failed")?
            .flush();
    }

    Ok(phys_addr)
}

/// Unmap a page and optionally free the backing frame.
pub fn unmap_page(virt: VirtAddr, free_frame: bool) {
    let page = Page::<Size4KiB>::containing_address(virt);
    unsafe {
        let mut mapper = offset_page_table();
        if let Ok((frame, flush)) = mapper.unmap(page) {
            flush.flush();
            if free_frame {
                super::pmm::free_frame(frame.start_address());
            }
        }
    }
}

/// Translate a virtual address to its physical address using the active page table.
pub fn translate(virt: VirtAddr) -> Option<PhysAddr> {
    use x86_64::structures::paging::mapper::TranslateResult;
    use x86_64::structures::paging::Translate;
    unsafe {
        let mapper = offset_page_table();
        match mapper.translate(virt) {
            TranslateResult::Mapped { frame, offset, .. } => {
                Some(frame.start_address() + offset)
            }
            _ => None,
        }
    }
}
