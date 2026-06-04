//! File descriptor table per process.

use alloc::vec::Vec;
use alloc::sync::Arc;
use sync::Mutex;

/// Abstract file object — backed by a VFS node, pipe, device, etc.
pub trait FileObject: Send + Sync {
    fn read(&self, buf: &mut [u8]) -> Result<usize, i64>;
    fn write(&self, buf: &[u8])    -> Result<usize, i64>;
    fn close(&self) {}
    fn is_tty(&self) -> bool { false }
}

#[derive(Clone)]
pub struct Fd(pub Arc<dyn FileObject>);

/// Per-process file descriptor table (indexed by fd number).
pub struct FdTable {
    fds: Vec<Option<Fd>>,
}

impl FdTable {
    pub fn new() -> Self {
        Self { fds: Vec::new() }
    }

    /// Insert a FileObject and return its fd number.
    pub fn insert(&mut self, file: Arc<dyn FileObject>) -> usize {
        // Find lowest free slot
        for (i, slot) in self.fds.iter_mut().enumerate() {
            if slot.is_none() {
                *slot = Some(Fd(file));
                return i;
            }
        }
        self.fds.push(Some(Fd(file)));
        self.fds.len() - 1
    }

    /// Insert at a specific fd, replacing whatever was there.
    pub fn insert_at(&mut self, fd: usize, file: Arc<dyn FileObject>) {
        if fd >= self.fds.len() {
            self.fds.resize(fd + 1, None);
        }
        self.fds[fd] = Some(Fd(file));
    }

    pub fn get(&self, fd: usize) -> Option<&Fd> {
        self.fds.get(fd)?.as_ref()
    }

    pub fn close(&mut self, fd: usize) -> bool {
        if let Some(slot) = self.fds.get_mut(fd) {
            if slot.is_some() {
                slot.take();
                return true;
            }
        }
        false
    }

    /// Clone the table (for fork — shared Arcs).
    pub fn clone_for_fork(&self) -> Self {
        Self { fds: self.fds.clone() }
    }

    pub fn len(&self) -> usize { self.fds.len() }
}
