//! Stat structure shared between kernel and userland.

#[derive(Debug, Clone, Copy, Default)]
#[repr(C)]
pub struct Stat {
    pub ino:   u64,
    pub mode:  u32,
    pub nlink: u32,
    pub size:  u64,
    pub atime: u64,
    pub mtime: u64,
    pub ctime: u64,
}

pub const S_IFMT:  u32 = 0xF000;
pub const S_IFREG: u32 = 0x8000;
pub const S_IFDIR: u32 = 0x4000;
pub const S_IFCHR: u32 = 0x2000;

pub fn s_isreg(m: u32) -> bool { m & S_IFMT == S_IFREG }
pub fn s_isdir(m: u32) -> bool { m & S_IFMT == S_IFDIR }
pub fn s_ischr(m: u32) -> bool { m & S_IFMT == S_IFCHR }
