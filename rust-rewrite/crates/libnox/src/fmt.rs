//! Formatting: print!/println!/eprint!/eprintln! + write! support.

use core::fmt::{self, Write};
use crate::syscall::sys_write;

/// Write to fd.
pub struct FdWriter(pub i32);

impl fmt::Write for FdWriter {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        sys_write(self.0, s.as_bytes());
        Ok(())
    }
}

#[macro_export]
macro_rules! print {
    ($($arg:tt)*) => {{
        use core::fmt::Write;
        let _ = write!($crate::fmt::FdWriter(1), $($arg)*);
    }};
}

#[macro_export]
macro_rules! println {
    () => ($crate::print!("\n"));
    ($($arg:tt)*) => {{
        use core::fmt::Write;
        let _ = write!($crate::fmt::FdWriter(1), $($arg)*);
        $crate::io::putchar(b'\n');
    }};
}

#[macro_export]
macro_rules! eprint {
    ($($arg:tt)*) => {{
        use core::fmt::Write;
        let _ = write!($crate::fmt::FdWriter(2), $($arg)*);
    }};
}

#[macro_export]
macro_rules! eprintln {
    () => ($crate::eprint!("\n"));
    ($($arg:tt)*) => {{
        use core::fmt::Write;
        let _ = write!($crate::fmt::FdWriter(2), $($arg)*);
        $crate::syscall::sys_write(2, b"\n");
    }};
}
