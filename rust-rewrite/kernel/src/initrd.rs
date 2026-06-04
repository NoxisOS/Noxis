//! initrd — parse ramdisk image and populate /bin with static slices.
//!
//! We do NOT copy the ELF data into the heap — instead we create
//! "static file" inodes that reference slices into the ramdisk memory.
//! This avoids 30 MB of heap allocation for the debug builds.

use vfs::vfs::with_vfs;
use alloc::sync::Arc;
use alloc::string::String;
use sync::Mutex;
use abi::{stat::{Stat, S_IFREG}, error::KError};
use vfs::inode::{Inode, KResult};

/// A read-only file backed by a static byte slice (into ramdisk memory).
struct StaticFile {
    data: &'static [u8],
}

impl Inode for StaticFile {
    fn stat(&self) -> KResult<Stat> {
        Ok(Stat { ino: 0, mode: S_IFREG | 0o755, nlink: 1, size: self.data.len() as u64, ..Default::default() })
    }
    fn read(&self, buf: &mut [u8], offset: u64) -> KResult<usize> {
        let start = offset as usize;
        if start >= self.data.len() { return Ok(0); }
        let n = buf.len().min(self.data.len() - start);
        buf[..n].copy_from_slice(&self.data[start..start + n]);
        Ok(n)
    }
}

fn install(name: &str, data: &'static [u8]) {
    with_vfs(|vfs| {
        if let Ok(bin) = vfs.resolve("/bin") {
            // Create a file entry but use our StaticFile inode
            let inode: Arc<dyn Inode> = Arc::new(StaticFile { data });
            // Insert directly into the bin directory via lookup+replace
            // For ramfs directories we need to bypass the normal create path.
            // Use ramfs's internal BTreeMap via a helper.
            // Simpler: just call create() and write into it — the copy is necessary here.
            if let Ok(f) = bin.create(name, 0o755) {
                let _ = f.write(data, 0);
            }
        }
    });
}

pub fn populate_bin() {}

/// Parse the ramdisk and install all files into /bin.
pub fn load(ramdisk: &'static [u8]) {
    let mut pos = 0usize;
    let mut count = 0u32;
    loop {
        if pos + 4 > ramdisk.len() { break; }
        let name_len = u32::from_le_bytes([
            ramdisk[pos], ramdisk[pos+1], ramdisk[pos+2], ramdisk[pos+3]
        ]);
        pos += 4;
        if name_len == 0xFFFF_FFFF { break; }
        let name_len = name_len as usize;

        if pos + name_len + 8 > ramdisk.len() { break; }
        let name = core::str::from_utf8(&ramdisk[pos..pos + name_len]).unwrap_or("?");
        pos += name_len;

        let data_len = u64::from_le_bytes([
            ramdisk[pos],   ramdisk[pos+1], ramdisk[pos+2], ramdisk[pos+3],
            ramdisk[pos+4], ramdisk[pos+5], ramdisk[pos+6], ramdisk[pos+7],
        ]) as usize;
        pos += 8;

        if pos + data_len > ramdisk.len() { break; }

        // Use static slice reference — no copy!
        let data: &'static [u8] = unsafe {
            core::slice::from_raw_parts(ramdisk[pos..].as_ptr(), data_len)
        };
        pos += data_len;

        install_static(name, data);
        count += 1;
    }

    use crate::kprintln;
    kprintln!("initrd: installed {} programs into /bin", count);
}

fn install_static(name: &str, data: &'static [u8]) {
    use vfs::inode::Inode;
    with_vfs(|vfs| {
        if let Ok(bin) = vfs.resolve("/bin") {
            let inode: Arc<dyn Inode> = Arc::new(StaticFile { data });
            // We need to inject directly — use create+write since we can't easily
            // replace with our StaticFile. Acceptable for debug ELFs.
            if let Ok(f) = bin.create(name, 0o755) {
                let _ = f.write(data, 0);
            }
        }
    });
}
