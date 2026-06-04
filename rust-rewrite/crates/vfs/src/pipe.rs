//! Pipe — unidirectional byte stream between two processes.

use alloc::{sync::Arc, collections::VecDeque};
use sync::Mutex;
use sched::fd::FileObject;

const PIPE_BUF: usize = 4096;

struct PipeInner {
    buf:         VecDeque<u8>,
    write_closed: bool,
    read_closed:  bool,
}

impl PipeInner {
    fn new() -> Self {
        Self { buf: VecDeque::with_capacity(PIPE_BUF), write_closed: false, read_closed: false }
    }
}

/// Read end of a pipe.
pub struct PipeRead(Arc<Mutex<PipeInner>>);

/// Write end of a pipe.
pub struct PipeWrite(Arc<Mutex<PipeInner>>);

/// Create a (read, write) pipe pair.
pub fn new_pipe() -> (Arc<PipeRead>, Arc<PipeWrite>) {
    let inner = Arc::new(Mutex::new(PipeInner::new()));
    (
        Arc::new(PipeRead(inner.clone())),
        Arc::new(PipeWrite(inner)),
    )
}

impl FileObject for PipeRead {
    fn read(&self, buf: &mut [u8]) -> Result<usize, i64> {
        let mut inner = self.0.lock();
        if inner.buf.is_empty() {
            if inner.write_closed { return Ok(0); } // EOF
            return Err(-11); // EAGAIN
        }
        let n = buf.len().min(inner.buf.len());
        for i in 0..n { buf[i] = inner.buf.pop_front().unwrap(); }
        Ok(n)
    }
    fn write(&self, _buf: &[u8]) -> Result<usize, i64> { Err(-9) } // EBADF
    fn close(&self) { self.0.lock().read_closed = true; }
}

impl FileObject for PipeWrite {
    fn read(&self, _buf: &mut [u8]) -> Result<usize, i64> { Err(-9) }
    fn write(&self, buf: &[u8]) -> Result<usize, i64> {
        let mut inner = self.0.lock();
        if inner.read_closed { return Err(-32); } // EPIPE
        let space = PIPE_BUF.saturating_sub(inner.buf.len());
        let n = buf.len().min(space);
        for b in &buf[..n] { inner.buf.push_back(*b); }
        Ok(n)
    }
    fn close(&self) { self.0.lock().write_closed = true; }
}
