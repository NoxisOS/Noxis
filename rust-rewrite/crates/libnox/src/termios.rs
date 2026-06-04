//! Termios wrappers — talk to /dev/tty via ioctl (stub for now).

// IOCTL commands
pub const TCGETS: u32 = 0x5401;
pub const TCSETS: u32 = 0x5402;

#[derive(Clone, Copy, Default)]
#[repr(C)]
pub struct Termios {
    pub c_lflag: u32,
    pub c_cc:    [u8; 8],
}

pub const ICANON: u32 = 1 << 0;
pub const ECHO:   u32 = 1 << 1;
pub const ECHOE:  u32 = 1 << 2;
pub const ISIG:   u32 = 1 << 3;

pub fn tcgetattr(fd: i32, t: &mut Termios) -> i32 {
    unsafe {
        crate::syscall::syscall2(
            abi::syscall::Syscall::Ioctl as u64,
            fd as u64,
            t as *mut _ as u64,
        ) as i32
    }
}

pub fn tcsetattr(fd: i32, t: &Termios) -> i32 {
    unsafe {
        crate::syscall::syscall2(
            abi::syscall::Syscall::Ioctl as u64,
            fd as u64,
            t as *const _ as u64,
        ) as i32
    }
}

pub fn set_raw(fd: i32) {
    let mut t = Termios::default();
    tcgetattr(fd, &mut t);
    t.c_lflag &= !(ICANON | ECHO | ECHOE | ISIG);
    tcsetattr(fd, &t);
}

pub fn set_canonical(fd: i32) {
    let mut t = Termios::default();
    tcgetattr(fd, &mut t);
    t.c_lflag |= ICANON | ECHO | ECHOE | ISIG;
    tcsetattr(fd, &t);
}
