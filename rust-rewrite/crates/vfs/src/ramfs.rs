//! RamFS — in-memory filesystem backed by Vec<u8>.
//!
//! Used for the root filesystem and temporary mounts.

use alloc::{
    string::String, vec::Vec, sync::Arc,
    collections::BTreeMap,
};
use sync::Mutex;
use core::sync::atomic::{AtomicU64, Ordering};

use super::inode::{FileSystem, Inode, KResult};
use abi::{stat::{Stat, S_IFREG, S_IFDIR}, dirent::{Dirent, DT_REG, DT_DIR}, error::KError};

static NEXT_INO: AtomicU64 = AtomicU64::new(1);
fn alloc_ino() -> u64 { NEXT_INO.fetch_add(1, Ordering::Relaxed) }

// ── File inode ────────────────────────────────────────────────────────────────

struct RamFile {
    ino:  u64,
    data: Mutex<Vec<u8>>,
}

impl Inode for RamFile {
    fn stat(&self) -> KResult<Stat> {
        let size = self.data.lock().len() as u64;
        Ok(Stat { ino: self.ino, mode: S_IFREG | 0o644, nlink: 1, size, ..Default::default() })
    }
    fn read(&self, buf: &mut [u8], offset: u64) -> KResult<usize> {
        let data = self.data.lock();
        let start = offset as usize;
        if start >= data.len() { return Ok(0); }
        let n = buf.len().min(data.len() - start);
        buf[..n].copy_from_slice(&data[start..start + n]);
        Ok(n)
    }
    fn write(&self, buf: &[u8], offset: u64) -> KResult<usize> {
        let mut data = self.data.lock();
        let start = offset as usize;
        if start + buf.len() > data.len() {
            data.resize(start + buf.len(), 0);
        }
        data[start..start + buf.len()].copy_from_slice(buf);
        Ok(buf.len())
    }
    fn truncate(&self, size: u64) -> KResult<()> {
        self.data.lock().truncate(size as usize);
        Ok(())
    }
}

// ── Directory inode ───────────────────────────────────────────────────────────

struct RamDir {
    ino:      u64,
    children: Mutex<BTreeMap<String, Arc<dyn Inode>>>,
}

impl Inode for RamDir {
    fn stat(&self) -> KResult<Stat> {
        Ok(Stat { ino: self.ino, mode: S_IFDIR | 0o755, nlink: 2, ..Default::default() })
    }
    fn lookup(&self, name: &str) -> KResult<Arc<dyn Inode>> {
        self.children.lock().get(name).cloned().ok_or(KError::NoEnt)
    }
    fn create(&self, name: &str, _mode: u32) -> KResult<Arc<dyn Inode>> {
        let f: Arc<dyn Inode> = Arc::new(RamFile {
            ino:  alloc_ino(),
            data: Mutex::new(Vec::new()),
        });
        self.children.lock().insert(String::from(name), f.clone());
        Ok(f)
    }
    fn mkdir(&self, name: &str, _mode: u32) -> KResult<Arc<dyn Inode>> {
        let d: Arc<dyn Inode> = Arc::new(RamDir {
            ino:      alloc_ino(),
            children: Mutex::new(BTreeMap::new()),
        });
        self.children.lock().insert(String::from(name), d.clone());
        Ok(d)
    }
    fn unlink(&self, name: &str) -> KResult<()> {
        let mut c = self.children.lock();
        if c.remove(name).is_some() { Ok(()) } else { Err(KError::NoEnt) }
    }
    fn rename(&self, old: &str, new: &str) -> KResult<()> {
        let mut c = self.children.lock();
        let node = c.remove(old).ok_or(KError::NoEnt)?;
        c.insert(String::from(new), node);
        Ok(())
    }
    fn readdir(&self, offset: u64) -> KResult<Vec<Dirent>> {
        let c = self.children.lock();
        let entries: Vec<Dirent> = c.iter().skip(offset as usize).map(|(name, inode)| {
            let st = inode.stat().unwrap_or_default();
            let kind = if st.mode & 0xF000 == S_IFDIR { DT_DIR } else { DT_REG };
            let mut d = Dirent { ino: st.ino, kind, name: [0; 255] };
            let nb = name.len().min(254);
            d.name[..nb].copy_from_slice(&name.as_bytes()[..nb]);
            d
        }).collect();
        Ok(entries)
    }
}

// ── Filesystem ────────────────────────────────────────────────────────────────

pub struct RamFs {
    root: Arc<dyn Inode>,
}

impl RamFs {
    pub fn new() -> Self {
        Self {
            root: Arc::new(RamDir {
                ino:      alloc_ino(),
                children: Mutex::new(BTreeMap::new()),
            }),
        }
    }
}

impl FileSystem for RamFs {
    fn name(&self) -> &str { "ramfs" }
    fn root(&self) -> Arc<dyn Inode> { self.root.clone() }
}
