//! Basic I/O: print/println to stdout, read from stdin.

use crate::syscall::{sys_read, sys_write, raw_write};

pub fn write_str(s: &str) {
    sys_write(1, s.as_bytes());
}

pub fn write_bytes(buf: &[u8]) {
    sys_write(1, buf);
}

pub fn putchar(c: u8) {
    unsafe { raw_write(1, &c as *const u8, 1); }
}

pub fn read_byte() -> Option<u8> {
    let mut b = [0u8; 1];
    if sys_read(0, &mut b) == 1 { Some(b[0]) } else { None }
}

pub fn read_line(buf: &mut [u8]) -> usize {
    let mut n = 0;
    while n < buf.len() - 1 {
        match read_byte() {
            None        => break,
            Some(b'\n') => { buf[n] = b'\n'; n += 1; break; }
            Some(b)     => { buf[n] = b; n += 1; }
        }
    }
    buf[n] = 0;
    n
}
