//! TTY — terminal line discipline (termios-like).
//!
//! Supports canonical mode (line-buffered) and raw mode.
//! Sits between the keyboard driver and user-space read().

use sync::Mutex;

pub const NCCS: usize = 8;

bitflags::bitflags! {
    #[derive(Debug, Clone, Copy, Default)]
    pub struct LFlag: u32 {
        const ICANON = 1 << 0; // canonical (line-buffered) mode
        const ECHO   = 1 << 1; // echo input
        const ECHOE  = 1 << 2; // echo erase as BS SP BS
        const ISIG   = 1 << 3; // generate signals (SIGINT on ^C)
    }
}

#[derive(Debug, Clone, Copy)]
pub struct Termios {
    pub c_lflag: LFlag,
    pub c_cc:    [u8; NCCS],
}

impl Default for Termios {
    fn default() -> Self {
        let mut cc = [0u8; NCCS];
        cc[0] = b'\n'; // VEOF / VEOL
        cc[1] = 0x08;  // VERASE (backspace)
        cc[2] = 0x03;  // VINTR  (^C)
        cc[3] = 0x1C;  // VQUIT  (^\)
        Self {
            c_lflag: LFlag::ICANON | LFlag::ECHO | LFlag::ECHOE | LFlag::ISIG,
            c_cc: cc,
        }
    }
}

const LINE_BUF_SIZE: usize = 512;

struct TtyState {
    termios:  Termios,
    line_buf: [u8; LINE_BUF_SIZE],
    line_len: usize,
    // completed lines waiting to be read
    read_buf: [u8; 4096],
    read_head: usize,
    read_tail: usize,
}

impl TtyState {
    const fn new() -> Self {
        Self {
            termios: Termios {
                c_lflag: LFlag::ICANON.union(LFlag::ECHO).union(LFlag::ECHOE).union(LFlag::ISIG),
                c_cc: [b'\n', 0x08, 0x03, 0x1C, 0, 0, 0, 0],
            },
            line_buf:  [0; LINE_BUF_SIZE],
            line_len:  0,
            read_buf:  [0; 4096],
            read_head: 0,
            read_tail: 0,
        }
    }

    fn push_read(&mut self, b: u8) {
        let next = (self.read_head + 1) % 4096;
        if next != self.read_tail {
            self.read_buf[self.read_head] = b;
            self.read_head = next;
        }
    }

    fn pop_read(&mut self) -> Option<u8> {
        if self.read_head == self.read_tail { return None; }
        let b = self.read_buf[self.read_tail];
        self.read_tail = (self.read_tail + 1) % 4096;
        Some(b)
    }

    /// Process one input byte through the line discipline.
    /// Returns true if a SIGINT should be sent.
    fn process(&mut self, c: u8) -> bool {
        if self.termios.c_lflag.contains(LFlag::ISIG) && c == self.termios.c_cc[2] {
            return true; // SIGINT
        }
        if self.termios.c_lflag.contains(LFlag::ICANON) {
            if c == self.termios.c_cc[1] {
                // Erase (backspace)
                if self.line_len > 0 {
                    self.line_len -= 1;
                    if self.termios.c_lflag.contains(LFlag::ECHOE) {
                        self.push_read(0x08);
                        self.push_read(b' ');
                        self.push_read(0x08);
                    }
                }
            } else if c == b'\n' || c == b'\r' || c == self.termios.c_cc[0] {
                // End of line
                if self.line_len < LINE_BUF_SIZE {
                    self.line_buf[self.line_len] = b'\n';
                    self.line_len += 1;
                }
                for i in 0..self.line_len { self.push_read(self.line_buf[i]); }
                self.line_len = 0;
                if self.termios.c_lflag.contains(LFlag::ECHO) { self.push_read(b'\n'); }
            } else {
                // Regular char
                if self.line_len < LINE_BUF_SIZE {
                    self.line_buf[self.line_len] = c;
                    self.line_len += 1;
                    if self.termios.c_lflag.contains(LFlag::ECHO) { self.push_read(c); }
                }
            }
        } else {
            // Raw mode
            self.push_read(c);
            if self.termios.c_lflag.contains(LFlag::ECHO) { self.push_read(c); }
        }
        false
    }
}

static TTY: Mutex<TtyState> = Mutex::new(TtyState::new());

/// Feed a raw character into the TTY. Returns true → send SIGINT.
pub fn input(c: u8) -> bool { TTY.lock().process(c) }

/// Read up to `buf.len()` bytes from the TTY (non-blocking).
pub fn read(buf: &mut [u8]) -> usize {
    let mut tty = TTY.lock();
    let mut n = 0;
    while n < buf.len() {
        match tty.pop_read() {
            Some(b) => { buf[n] = b; n += 1; }
            None    => break,
        }
    }
    n
}

/// Check if any data is ready to read.
pub fn has_data() -> bool {
    let tty = TTY.lock();
    tty.read_head != tty.read_tail
}

pub fn get_termios() -> Termios { TTY.lock().termios }
pub fn set_termios(t: Termios)  { TTY.lock().termios = t; }
