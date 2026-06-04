//! Syscall numbers and ABI (SysV x86-64: num=rax, args=rdi/rsi/rdx/r10/r8/r9).

#[repr(u64)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Syscall {
    Read       = 0,
    Write      = 1,
    Open       = 2,
    Close      = 3,
    Stat       = 4,
    Seek       = 5,
    Exit       = 6,
    Fork       = 7,
    Exec       = 8,
    Waitpid    = 9,
    Getpid     = 10,
    Getppid    = 11,
    Brk        = 12,
    Mmap       = 13,
    Munmap     = 14,
    Sleep      = 15,
    Uptime     = 16,
    Chdir      = 17,
    Getcwd     = 18,
    Mkdir      = 19,
    Unlink     = 20,
    Rename     = 21,
    Getdents   = 22,
    Dup        = 23,
    Dup2       = 24,
    Pipe       = 25,
    Ioctl      = 26,
    Getenv     = 27,
    Setenv     = 28,
    Unsetenv   = 29,
    GetenvAt   = 30,
    Kill       = 31,
    Setfg      = 32,
}

impl Syscall {
    pub fn from_u64(n: u64) -> Option<Self> {
        if n <= 32 {
            // SAFETY: repr(u64), value bounds-checked
            Some(unsafe { core::mem::transmute(n) })
        } else {
            None
        }
    }
}
