//! readline — line editor with history, arrow keys.

use alloc::{string::String, vec::Vec};
use crate::io::{putchar, read_byte, write_str};

const HISTORY_MAX: usize = 50;

pub struct Readline {
    history: Vec<String>,
}

impl Readline {
    pub fn new() -> Self { Self { history: Vec::new() } }

    pub fn read(&mut self, prompt: &str) -> Option<String> {
        write_str(prompt);

        let mut line = String::new();
        let mut hist_idx: Option<usize> = None;
        let mut cursor = 0usize;

        loop {
            let b = read_byte()?;

            match b {
                b'\r' | b'\n' => {
                    putchar(b'\n');
                    break;
                }
                0x08 | 0x7F => {
                    // Backspace
                    if cursor > 0 {
                        cursor -= 1;
                        line.remove(cursor);
                        // Redraw
                        write_str("\x08 \x08");
                    }
                }
                0x01 => { /* Ctrl-A: move to start (TODO) */ }
                0x05 => { /* Ctrl-E: move to end (TODO) */ }
                0x03 => {
                    // Ctrl-C
                    write_str("^C\n");
                    return Some(String::new());
                }
                0x04 => {
                    // Ctrl-D (EOF)
                    if line.is_empty() { return None; }
                }
                0x1B => {
                    // Escape sequence
                    let b2 = read_byte().unwrap_or(0);
                    let b3 = read_byte().unwrap_or(0);
                    if b2 == b'[' {
                        match b3 {
                            b'A' => {
                                // Up arrow — previous history
                                let new_idx = match hist_idx {
                                    None if !self.history.is_empty() => Some(self.history.len() - 1),
                                    Some(i) if i > 0 => Some(i - 1),
                                    _ => continue,
                                };
                                if let Some(idx) = new_idx {
                                    // Clear current line
                                    for _ in 0..line.len() { write_str("\x08 \x08"); }
                                    line = self.history[idx].clone();
                                    cursor = line.len();
                                    write_str(&line);
                                    hist_idx = Some(idx);
                                }
                            }
                            b'B' => {
                                // Down arrow
                                if let Some(idx) = hist_idx {
                                    for _ in 0..line.len() { write_str("\x08 \x08"); }
                                    if idx + 1 < self.history.len() {
                                        hist_idx = Some(idx + 1);
                                        line = self.history[idx + 1].clone();
                                    } else {
                                        hist_idx = None;
                                        line = String::new();
                                    }
                                    cursor = line.len();
                                    write_str(&line);
                                }
                            }
                            b'C' => { /* Right arrow TODO */ }
                            b'D' => {
                                // Left arrow
                                if cursor > 0 {
                                    cursor -= 1;
                                    write_str("\x1b[D");
                                }
                            }
                            _ => {}
                        }
                    }
                }
                32..=126 => {
                    // Printable ASCII
                    line.insert(cursor, b as char);
                    cursor += 1;
                    putchar(b);
                }
                _ => {}
            }
        }

        if !line.is_empty() {
            if self.history.last().map(|s| s.as_str()) != Some(&line) {
                if self.history.len() >= HISTORY_MAX { self.history.remove(0); }
                self.history.push(line.clone());
            }
        }
        Some(line)
    }
}
