//! Core VFS traits: FileSystem and Inode.

use alloc::{string::String, vec::Vec, sync::Arc, boxed::Box};
use abi::{stat::Stat, dirent::Dirent, error::KError};

pub type KResult<T> = Result<T, KError>;

/// A mounted filesystem.
pub trait FileSystem: Send + Sync {
    fn name(&self) -> &str;
    fn root(&self) -> Arc<dyn Inode>;
}

/// An inode — file, directory, device, pipe, etc.
pub trait Inode: Send + Sync {
    fn stat(&self) -> KResult<Stat>;

    // File operations
    fn read(&self,  buf: &mut [u8], offset: u64) -> KResult<usize> { Err(KError::Inval) }
    fn write(&self, buf: &[u8],     offset: u64) -> KResult<usize> { Err(KError::Inval) }
    fn truncate(&self, size: u64)                -> KResult<()>    { Err(KError::Inval) }

    // Directory operations
    fn lookup(&self, name: &str)           -> KResult<Arc<dyn Inode>> { Err(KError::NotDir) }
    fn create(&self, name: &str, mode: u32)-> KResult<Arc<dyn Inode>> { Err(KError::NotDir) }
    fn mkdir(&self,  name: &str, mode: u32)-> KResult<Arc<dyn Inode>> { Err(KError::NotDir) }
    fn unlink(&self, name: &str)           -> KResult<()>             { Err(KError::NotDir) }
    fn rename(&self, old: &str, new: &str) -> KResult<()>             { Err(KError::NotDir) }
    fn readdir(&self, offset: u64)         -> KResult<Vec<Dirent>>    { Err(KError::NotDir) }

    // Device / special
    fn ioctl(&self, cmd: u32, arg: u64)    -> KResult<i64>            { Err(KError::Inval) }
    fn is_tty(&self)                       -> bool                    { false }
}

/// Open file handle — tracks inode + position + flags.
pub struct OpenFile {
    pub inode:  Arc<dyn Inode>,
    pub offset: u64,
    pub flags:  u32,
}

impl OpenFile {
    pub fn new(inode: Arc<dyn Inode>, flags: u32) -> Self {
        Self { inode, offset: 0, flags }
    }

    pub fn read(&mut self, buf: &mut [u8]) -> KResult<usize> {
        let n = self.inode.read(buf, self.offset)?;
        self.offset += n as u64;
        Ok(n)
    }

    pub fn write(&mut self, buf: &[u8]) -> KResult<usize> {
        let n = self.inode.write(buf, self.offset)?;
        self.offset += n as u64;
        Ok(n)
    }

    pub fn seek(&mut self, pos: u64) { self.offset = pos; }
}

// FileObject adapter so OpenFile plugs into sched::FdTable
use sched::fd::FileObject;

/// Wrapper so we can impl FileObject on OpenFile (avoiding orphan rule).
pub struct OpenFileFd(pub sync::Mutex<OpenFile>);

impl FileObject for OpenFileFd {
    fn read(&self, buf: &mut [u8]) -> Result<usize, i64> {
        self.0.lock().read(buf).map_err(|e| e as i64)
    }
    fn write(&self, buf: &[u8]) -> Result<usize, i64> {
        self.0.lock().write(buf).map_err(|e| e as i64)
    }
    fn is_tty(&self) -> bool {
        self.0.lock().inode.is_tty()
    }
}

impl OpenFileFd {
    pub fn new(inode: Arc<dyn Inode>, flags: u32) -> Self {
        Self(sync::Mutex::new(OpenFile::new(inode, flags)))
    }
}
