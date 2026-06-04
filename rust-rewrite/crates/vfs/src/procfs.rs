//! procfs — /proc virtual filesystem.

use alloc::{string::String, vec::Vec, sync::Arc, format};
use super::inode::{FileSystem, Inode, KResult};
use abi::{stat::{Stat, S_IFREG, S_IFDIR}, dirent::{Dirent, DT_REG, DT_DIR}, error::KError};

struct ProcRoot;
impl Inode for ProcRoot {
    fn stat(&self) -> KResult<Stat> {
        Ok(Stat { ino: 0xFFFFFFFE, mode: S_IFDIR | 0o555, nlink: 2, ..Default::default() })
    }
    fn lookup(&self, name: &str) -> KResult<Arc<dyn Inode>> {
        match name {
            "uptime"  => Ok(Arc::new(ProcUptime)),
            "meminfo" => Ok(Arc::new(ProcMeminfo)),
            _         => Err(KError::NoEnt),
        }
    }
    fn readdir(&self, _offset: u64) -> KResult<Vec<Dirent>> {
        fn ent(name: &str, ino: u64) -> Dirent {
            let mut d = Dirent { ino, kind: DT_REG, name: [0; 255] };
            let n = name.len().min(254);
            d.name[..n].copy_from_slice(&name.as_bytes()[..n]);
            d
        }
        Ok(alloc::vec![ent("uptime", 1), ent("meminfo", 2)])
    }
}

struct ProcUptime;
impl Inode for ProcUptime {
    fn stat(&self) -> KResult<Stat> {
        Ok(Stat { ino: 1, mode: S_IFREG | 0o444, nlink: 1, ..Default::default() })
    }
    fn read(&self, buf: &mut [u8], offset: u64) -> KResult<usize> {
        // uptime in seconds — we don't have a real clock yet, use 0
        let s = format!("0.00 0.00\n");
        let b = s.as_bytes();
        let start = offset as usize;
        if start >= b.len() { return Ok(0); }
        let n = buf.len().min(b.len() - start);
        buf[..n].copy_from_slice(&b[start..start + n]);
        Ok(n)
    }
}

struct ProcMeminfo;
impl Inode for ProcMeminfo {
    fn stat(&self) -> KResult<Stat> {
        Ok(Stat { ino: 2, mode: S_IFREG | 0o444, nlink: 1, ..Default::default() })
    }
    fn read(&self, buf: &mut [u8], offset: u64) -> KResult<usize> {
        let (used, free) = mm::heap::stats();
        let s = format!(
            "HeapUsed: {} kB\nHeapFree: {} kB\n",
            used / 1024, free / 1024
        );
        let b = s.as_bytes();
        let start = offset as usize;
        if start >= b.len() { return Ok(0); }
        let n = buf.len().min(b.len() - start);
        buf[..n].copy_from_slice(&b[start..start + n]);
        Ok(n)
    }
}

pub struct ProcFs;
impl FileSystem for ProcFs {
    fn name(&self) -> &str { "procfs" }
    fn root(&self) -> Arc<dyn Inode> { Arc::new(ProcRoot) }
}
