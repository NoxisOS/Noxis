//! VGA text mode 80×25 — fallback when no UEFI framebuffer is available.
//!
//! With UEFI/GOP we get a real pixel framebuffer; this module is a safety
//! net for QEMU runs without GOP and for early boot diagnostics.

use core::fmt;
use sync::Mutex;

const VGA_ADDR: usize = 0xB8000;
const COLS: usize = 80;
const ROWS: usize = 25;

#[derive(Clone, Copy)]
#[repr(u8)]
pub enum Color {
    Black    = 0,  Blue    = 1,  Green   = 2,  Cyan    = 3,
    Red      = 4,  Magenta = 5,  Brown   = 6,  White   = 7,
    BrightBlack   = 8,  BrightBlue    = 9,  BrightGreen  = 10,
    BrightCyan    = 11, BrightRed     = 12, BrightMagenta = 13,
    Yellow        = 14, BrightWhite   = 15,
}

pub struct VgaWriter {
    col:  usize,
    row:  usize,
    attr: u8,
    buf:  *mut u16,
}

unsafe impl Send for VgaWriter {}

impl VgaWriter {
    /// # Safety: fixed VGA address, use only when VGA text mode is active.
    pub unsafe fn new() -> Self {
        let w = VgaWriter {
            col: 0, row: 0,
            attr: (Color::Black as u8) << 4 | Color::BrightWhite as u8,
            buf: VGA_ADDR as *mut u16,
        };
        w.clear();
        w
    }

    fn clear(&self) {
        let blank = self.entry(b' ');
        for i in 0..(COLS * ROWS) {
            unsafe { self.buf.add(i).write_volatile(blank) };
        }
    }

    fn entry(&self, ch: u8) -> u16 { ((self.attr as u16) << 8) | ch as u16 }

    fn scroll(&mut self) {
        unsafe {
            core::ptr::copy(self.buf.add(COLS), self.buf, COLS * (ROWS - 1));
            let blank = self.entry(b' ');
            for i in (COLS * (ROWS - 1))..(COLS * ROWS) {
                self.buf.add(i).write_volatile(blank);
            }
        }
        self.row = ROWS - 1;
    }

    fn put_byte(&mut self, b: u8) {
        match b {
            b'\n' => { self.col = 0; self.row += 1; if self.row >= ROWS { self.scroll(); } }
            b'\r' => { self.col = 0; }
            _ => {
                let idx = self.row * COLS + self.col;
                unsafe { self.buf.add(idx).write_volatile(self.entry(b)) };
                self.col += 1;
                if self.col >= COLS {
                    self.col = 0;
                    self.row += 1;
                    if self.row >= ROWS { self.scroll(); }
                }
            }
        }
    }

    pub fn set_color(&mut self, fg: Color, bg: Color) {
        self.attr = ((bg as u8) << 4) | (fg as u8);
    }
}

impl fmt::Write for VgaWriter {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        for b in s.bytes() { self.put_byte(b); }
        Ok(())
    }
}

use sync::Lazy;
pub static VGA: Lazy<Mutex<VgaWriter>> = Lazy::new(|| {
    Mutex::new(unsafe { VgaWriter::new() })
});

#[macro_export]
macro_rules! kprint {
    ($($arg:tt)*) => {{
        use core::fmt::Write;
        let _ = write!($crate::vga::VGA.lock(), $($arg)*);
    }};
}

#[macro_export]
macro_rules! kprintln {
    () => ($crate::kprint!("\n"));
    ($($arg:tt)*) => ($crate::kprint!("{}\n", format_args!($($arg)*)));
}
