//! Filesystem helpers.

use alloc::{string::String, vec::Vec};
use abi::{stat::Stat, dirent::Dirent};
use crate::syscall::*;

pub fn read_file(path: &str) -> Option<Vec<u8>> {
    let fd = sys_open(path, 0);
    if fd < 0 { return None; }
    let mut out = Vec::new();
    let mut buf = [0u8; 512];
    loop {
        let n = sys_read(fd, &mut buf);
        if n <= 0 { break; }
        out.extend_from_slice(&buf[..n as usize]);
    }
    sys_close(fd);
    Some(out)
}

pub fn write_file(path: &str, data: &[u8]) -> bool {
    let fd = sys_open(path, 0x201); // O_WRONLY|O_CREAT
    if fd < 0 { return false; }
    let mut off = 0;
    while off < data.len() {
        let n = sys_write(fd, &data[off..]);
        if n <= 0 { break; }
        off += n as usize;
    }
    sys_close(fd);
    true
}

pub fn listdir(path: &str) -> Vec<String> {
    let fd = sys_open(path, 0);
    if fd < 0 { return Vec::new(); }
    let mut dirents = [Dirent { ino: 0, kind: 0, name: [0; 255] }; 64];
    let n = sys_getdents(fd, &mut dirents);
    sys_close(fd);
    if n < 0 { return Vec::new(); }
    dirents[..n as usize].iter().map(|d| {
        let end = d.name.iter().position(|&b| b == 0).unwrap_or(255);
        String::from(core::str::from_utf8(&d.name[..end]).unwrap_or(""))
    }).collect()
}

pub fn stat(path: &str) -> Option<Stat> {
    let mut st = Stat::default();
    if sys_stat(path, &mut st) == 0 { Some(st) } else { None }
}

pub fn getcwd() -> String {
    let mut buf = [0u8; 512];
    sys_getcwd(&mut buf);
    let end = buf.iter().position(|&b| b == 0).unwrap_or(512);
    String::from(core::str::from_utf8(&buf[..end]).unwrap_or("/"))
}
