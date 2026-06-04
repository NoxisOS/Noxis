//! Kernel error codes (typed errno equivalent).

#[repr(i64)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum KError {
    Ok          =  0,
    Perm        = -1,
    NoEnt       = -2,
    Io          = -5,
    BadFd       = -9,
    NoMem       = -12,
    Fault       = -14,
    Busy        = -16,
    Exist       = -17,
    NotDir      = -20,
    IsDir       = -21,
    Inval       = -22,
    NoSpc       = -28,
    Range       = -34,
    NameTooLong = -36,
    NoSys       = -38,
    NotEmpty    = -39,
    Again       = -11,
    Pipe        = -32,
}

impl KError {
    pub fn as_i64(self) -> i64 { self as i64 }
}
