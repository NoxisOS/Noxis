//! devfs — /dev virtual filesystem (null, zero, tty, random).

use alloc::{string::String, vec::Vec, sync::Arc};
use super::inode::{FileSystem, Inode, KResult};
use abi::{stat::{Stat, S_IFCHR, S_IFDIR}, dirent::{Dirent, DT_CHR, DT_DIR}, error::KError};

// ── Devices ───────────────────────────────────────────────────────────────────

struct DevNull;
impl Inode for DevNull {
    fn stat(&self) -> KResult<Stat> {
        Ok(Stat { ino: 1, mode: S_IFCHR | 0o666, nlink: 1, ..Default::default() })
    }
    fn read(&self, _buf: &mut [u8], _off: u64) -> KResult<usize> { Ok(0) }
    fn write(&self, buf: &[u8], _off: u64)     -> KResult<usize> { Ok(buf.len()) }
}

struct DevZero;
impl Inode for DevZero {
    fn stat(&self) -> KResult<Stat> {
        Ok(Stat { ino: 2, mode: S_IFCHR | 0o666, nlink: 1, ..Default::default() })
    }
    fn read(&self, buf: &mut [u8], _off: u64) -> KResult<usize> {
        buf.fill(0); Ok(buf.len())
    }
    fn write(&self, buf: &[u8], _off: u64)    -> KResult<usize> { Ok(buf.len()) }
}

struct DevTty;
impl Inode for DevTty {
    fn stat(&self) -> KResult<Stat> {
        Ok(Stat { ino: 3, mode: S_IFCHR | 0o622, nlink: 1, ..Default::default() })
    }
    fn read(&self, buf: &mut [u8], _off: u64) -> KResult<usize> {
        Ok(drivers::tty::read(buf))
    }
    fn write(&self, buf: &[u8], _off: u64) -> KResult<usize> {
        // Output to serial + VGA
        use core::fmt::Write;
        let s = core::str::from_utf8(buf).unwrap_or("");
        let _ = write!(drivers::serial::COM1_SERIAL.lock(), "{}", s);
        Ok(buf.len())
    }
    fn is_tty(&self) -> bool { true }
}

/// Simple xorshift PRNG for /dev/random.
struct DevRandom(core::sync::atomic::AtomicU64);
impl DevRandom {
    fn next(&self) -> u64 {
        use core::sync::atomic::Ordering;
        let mut x = self.0.load(Ordering::Relaxed);
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        self.0.store(x, Ordering::Relaxed); x
    }
}
impl Inode for DevRandom {
    fn stat(&self) -> KResult<Stat> {
        Ok(Stat { ino: 4, mode: S_IFCHR | 0o444, nlink: 1, ..Default::default() })
    }
    fn read(&self, buf: &mut [u8], _off: u64) -> KResult<usize> {
        let mut i = 0;
        while i + 8 <= buf.len() {
            let v = self.next().to_le_bytes();
            buf[i..i+8].copy_from_slice(&v);
            i += 8;
        }
        while i < buf.len() {
            buf[i] = (self.next() & 0xFF) as u8;
            i += 1;
        }
        Ok(buf.len())
    }
    fn write(&self, buf: &[u8], _off: u64) -> KResult<usize> { Ok(buf.len()) }
}

// ── Root dir ──────────────────────────────────────────────────────────────────

struct DevRoot;
impl Inode for DevRoot {
    fn stat(&self) -> KResult<Stat> {
        Ok(Stat { ino: 0xFFFE0000, mode: S_IFDIR | 0o555, nlink: 2, ..Default::default() })
    }
    fn lookup(&self, name: &str) -> KResult<Arc<dyn Inode>> {
        match name {
            "null"   => Ok(Arc::new(DevNull)),
            "zero"   => Ok(Arc::new(DevZero)),
            "tty"    => Ok(Arc::new(DevTty)),
            "random" => Ok(Arc::new(DevRandom(core::sync::atomic::AtomicU64::new(0xDEAD_BEEF)))),
            _        => Err(KError::NoEnt),
        }
    }
    fn readdir(&self, _offset: u64) -> KResult<Vec<Dirent>> {
        fn ent(name: &str, ino: u64) -> Dirent {
            let mut d = Dirent { ino, kind: DT_CHR, name: [0; 255] };
            let n = name.len().min(254);
            d.name[..n].copy_from_slice(&name.as_bytes()[..n]);
            d
        }
        Ok(alloc::vec![
            ent("null", 1), ent("zero", 2), ent("tty", 3), ent("random", 4)
        ])
    }
}

pub struct DevFs;
impl FileSystem for DevFs {
    fn name(&self) -> &str { "devfs" }
    fn root(&self) -> Arc<dyn Inode> { Arc::new(DevRoot) }
}
