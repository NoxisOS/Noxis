//! PS/2 Keyboard driver — scancode set 1, ring buffer, basic US keymap.

use hal::port::Port;
use hal::pic;
use sync::Mutex;

const KBD_DATA: u16 = 0x60;
const KBD_STATUS: u16 = 0x64;
const BUF_SIZE: usize = 256;

/// Ring buffer for raw scancodes.
struct KbdBuffer {
    buf:  [u8; BUF_SIZE],
    head: usize,
    tail: usize,
}

impl KbdBuffer {
    const fn new() -> Self {
        Self { buf: [0; BUF_SIZE], head: 0, tail: 0 }
    }
    fn push(&mut self, sc: u8) {
        let next = (self.head + 1) % BUF_SIZE;
        if next != self.tail { // drop if full
            self.buf[self.head] = sc;
            self.head = next;
        }
    }
    fn pop(&mut self) -> Option<u8> {
        if self.head == self.tail { return None; }
        let sc = self.buf[self.tail];
        self.tail = (self.tail + 1) % BUF_SIZE;
        Some(sc)
    }
    fn is_empty(&self) -> bool { self.head == self.tail }
}

static BUFFER: Mutex<KbdBuffer> = Mutex::new(KbdBuffer::new());

/// Initialize keyboard driver and unmask IRQ1.
pub fn init() {
    hal::idt::register_kbd(on_irq);
    pic::unmask(1);
}

/// IRQ1 handler — reads one scancode and pushes to buffer.
fn on_irq() {
    let sc = unsafe { Port::<u8>::new(KBD_DATA).read() };
    BUFFER.lock().push(sc);
}

/// Read one raw scancode (non-blocking). Returns None if buffer is empty.
pub fn read_scancode() -> Option<u8> {
    BUFFER.lock().pop()
}

/// Translate scancode set 1 to ASCII (make codes only, US layout).
pub fn scancode_to_ascii(sc: u8, shift: bool) -> Option<char> {
    // Only make codes (bit 7 = 0 → key press)
    if sc & 0x80 != 0 { return None; }

    const MAP_LOWER: &[u8] = b"\x00\x1b1234567890-=\x08\tqwertyuiop[]\n\x00asdfghjkl;'`\x00\\zxcvbnm,./\x00*\x00 ";
    const MAP_UPPER: &[u8] = b"\x00\x1b!@#$%^&*()_+\x08\tQWERTYUIOP{}\n\x00ASDFGHJKL:\"~\x00|ZXCVBNM<>?\x00*\x00 ";

    let idx = sc as usize;
    let map = if shift { MAP_UPPER } else { MAP_LOWER };
    if idx < map.len() && map[idx] != 0 {
        Some(map[idx] as char)
    } else {
        None
    }
}

/// Check if scancode is a key-release event.
#[inline]
pub fn is_release(sc: u8) -> bool { sc & 0x80 != 0 }

/// Check if buffer has pending data.
#[inline]
pub fn has_data() -> bool { !BUFFER.lock().is_empty() }
