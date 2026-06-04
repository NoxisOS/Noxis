//! Directory entry (getdents).

#[derive(Debug, Clone, Copy)]
#[repr(C)]
pub struct Dirent {
    pub ino:  u64,
    pub kind: u8,        // 4=dir, 8=file, 2=char
    pub name: [u8; 255],
}

pub const DT_REG: u8 = 8;
pub const DT_DIR: u8 = 4;
pub const DT_CHR: u8 = 2;
