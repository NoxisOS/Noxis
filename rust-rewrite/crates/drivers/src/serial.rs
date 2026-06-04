//! UART 16550A — COM1 serial port (0x3F8).
//!
//! Implements `core::fmt::Write` so it works directly with `write!` / `writeln!`.
//! This is the primary debug output channel.

use hal::port::Port;
use sync::{Mutex, Lazy};
use core::fmt;

const COM1: u16 = 0x3F8;

pub struct Serial {
    data:     Port<u8>,
    int_en:   Port<u8>,
    fifo:     Port<u8>,
    line_ctl: Port<u8>,
    modem:    Port<u8>,
    line_st:  Port<u8>,
}

impl Serial {
    /// Initialize the UART at the given base port.
    /// # Safety: direct port access, call only once per UART.
    pub unsafe fn new(base: u16) -> Self {
        let s = Self {
            data:     Port::new(base),
            int_en:   Port::new(base + 1),
            fifo:     Port::new(base + 2),
            line_ctl: Port::new(base + 3),
            modem:    Port::new(base + 4),
            line_st:  Port::new(base + 5),
        };
        s.int_en.write(0x00);   // Disable interrupts
        s.line_ctl.write(0x80); // DLAB=1 to set baud divisor
        s.data.write(0x03);     // Baud 38400: divisor lo=3
        s.int_en.write(0x00);   //             divisor hi=0
        s.line_ctl.write(0x03); // 8 bits, 1 stop, no parity
        s.fifo.write(0xC7);     // Enable FIFO, clear, 14-byte threshold
        s.modem.write(0x0B);    // IRQs enabled, RTS/DSR set
        s
    }

    fn line_status(&self) -> u8 { unsafe { self.line_st.read() } }
    fn transmit_empty(&self) -> bool { self.line_status() & 0x20 != 0 }

    pub fn write_byte(&self, b: u8) {
        while !self.transmit_empty() {}
        unsafe { self.data.write(b) };
    }

    pub fn write_str_raw(&self, s: &str) {
        for b in s.bytes() {
            if b == b'\n' { self.write_byte(b'\r'); }
            self.write_byte(b);
        }
    }
}

impl fmt::Write for Serial {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        self.write_str_raw(s);
        Ok(())
    }
}

// ── Global singleton ─────────────────────────────────────────────────────────

pub static COM1_SERIAL: Lazy<Mutex<Serial>> = Lazy::new(|| {
    Mutex::new(unsafe { Serial::new(COM1) })
});

/// Print to COM1 (format like print!).
#[macro_export]
macro_rules! serial_print {
    ($($arg:tt)*) => {{
        use core::fmt::Write;
        let mut s = $crate::serial::COM1_SERIAL.lock();
        let _ = write!(s, $($arg)*);
    }};
}

#[macro_export]
macro_rules! serial_println {
    () => ($crate::serial_print!("\n"));
    ($($arg:tt)*) => ($crate::serial_print!("{}\n", format_args!($($arg)*)));
}
