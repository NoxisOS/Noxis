//! ELF64 loader — maps segments into user address space.
//!
//! Reads a statically linked ELF64 binary from a byte slice,
//! maps LOAD segments into user space via VMM, sets up stack.

use mm::vmm::{map_page, MapFlags};
use x86_64::VirtAddr;

const PT_LOAD:    u32 = 1;
const EHDR_SIZE:  usize = 64;
const PHDR_SIZE:  usize = 56;

#[repr(C, packed)]
struct Elf64Ehdr {
    e_ident:     [u8; 16],
    e_type:      u16,
    e_machine:   u16,
    e_version:   u32,
    e_entry:     u64,
    e_phoff:     u64,
    e_shoff:     u64,
    e_flags:     u32,
    e_ehsize:    u16,
    e_phentsize: u16,
    e_phnum:     u16,
    e_shentsize: u16,
    e_shnum:     u16,
    e_shstrndx:  u16,
}

#[repr(C, packed)]
struct Elf64Phdr {
    p_type:   u32,
    p_flags:  u32,
    p_offset: u64,
    p_vaddr:  u64,
    p_paddr:  u64,
    p_filesz: u64,
    p_memsz:  u64,
    p_align:  u64,
}

/// Load an ELF64 binary from `data` into the current address space.
/// Returns (entry_point, initial_brk) on success.
pub fn load(data: &[u8]) -> Result<(u64, u64), &'static str> {
    if data.len() < EHDR_SIZE { return Err("ELF too small"); }
    if &data[0..4] != b"\x7fELF" { return Err("Not an ELF"); }
    if data[4] != 2 { return Err("Not ELF64"); }

    let ehdr = unsafe { &*(data.as_ptr() as *const Elf64Ehdr) };
    let entry = ehdr.e_entry;
    let phoff = ehdr.e_phoff as usize;
    let phnum = ehdr.e_phnum as usize;

    let mut max_vaddr: u64 = 0;

    for i in 0..phnum {
        let off = phoff + i * PHDR_SIZE;
        if off + PHDR_SIZE > data.len() { break; }
        let phdr = unsafe { &*(data[off..].as_ptr() as *const Elf64Phdr) };

        if phdr.p_type != PT_LOAD { continue; }

        let vaddr    = phdr.p_vaddr;
        let filesz   = phdr.p_filesz as usize;
        let memsz    = phdr.p_memsz  as usize;
        let file_off = phdr.p_offset as usize;
        let flags    = phdr.p_flags;

        // Map pages for this segment
        let page_start = vaddr & !0xFFF;
        let page_end   = (vaddr + memsz as u64 + 0xFFF) & !0xFFF;

        let mut mf = MapFlags::PRESENT | MapFlags::USER;
        if flags & 2 != 0 { mf |= MapFlags::WRITABLE; }
        if flags & 1 == 0 { mf |= MapFlags::NO_EXECUTE; }

        let mut page_addr = page_start;
        while page_addr < page_end {
            map_page(VirtAddr::new(page_addr), None, mf)
                .map_err(|_| "Failed to map ELF segment")?;

            // Copy file data into the freshly mapped page
            let page_virt = page_addr;
            let copy_src_start = if page_virt < vaddr { 0 } else { (page_virt - vaddr) as usize };
            let copy_dst_start = if page_virt < vaddr { (vaddr - page_virt) as usize } else { 0 };

            if copy_src_start < filesz {
                let copy_len = (filesz - copy_src_start).min(0x1000 - copy_dst_start);
                let dst = (page_virt + copy_dst_start as u64) as *mut u8;
                let src = &data[file_off + copy_src_start..];
                unsafe { core::ptr::copy_nonoverlapping(src.as_ptr(), dst, copy_len); }
            }

            page_addr += 0x1000;
        }

        if vaddr + memsz as u64 > max_vaddr {
            max_vaddr = vaddr + memsz as u64;
        }
    }

    // Round brk up to next page
    let brk = (max_vaddr + 0xFFF) & !0xFFF;
    Ok((entry, brk))
}
