#![no_std]
#![no_main]
extern crate alloc;
extern crate libnox;
use libnox::*;

#[no_mangle]
pub extern "C" fn main(_argc: i32, _argv: *const *const u8) -> i32 {
    // Read /proc — list PIDs
    println!("  PID  NAME");
    let entries = listdir("/proc");
    for e in &entries {
        if e.chars().all(|c| c.is_ascii_digit()) {
            let status_path = alloc::format!("/proc/{}/status", e);
            if let Some(data) = libnox::fs::read_file(&status_path) {
                let s = core::str::from_utf8(&data).unwrap_or("");
                println!("  {}  {}", e, s.lines().next().unwrap_or("?"));
            } else {
                println!("  {}  ?", e);
            }
        }
    }
    0
}
