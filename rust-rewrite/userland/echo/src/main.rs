#![no_std]
#![no_main]
extern crate alloc;
extern crate libnox;

use libnox::*;

#[no_mangle]
pub extern "C" fn main(argc: i32, argv: *const *const u8) -> i32 {
    let args = unsafe { crate::collect_args(argc, argv) };
    let mut newline = true;
    let mut escape  = false;
    let mut start   = 1usize;

    while start < args.len() && args[start].starts_with('-') {
        let f = &args[start][1..];
        if f.chars().all(|c| c == 'n' || c == 'e') {
            if f.contains('n') { newline = false; }
            if f.contains('e') { escape  = true;  }
            start += 1;
        } else { break; }
    }

    for (i, arg) in args[start..].iter().enumerate() {
        if i > 0 { sys_write(1, b" "); }
        if escape { print_escape(arg); } else { write_str(arg); }
    }
    if newline { sys_write(1, b"\n"); }
    0
}

fn print_escape(s: &str) {
    let b = s.as_bytes();
    let mut i = 0;
    while i < b.len() {
        if b[i] == b'\\' && i + 1 < b.len() {
            i += 1;
            match b[i] {
                b'n'  => { sys_write(1, b"\n"); }
                b't'  => { sys_write(1, b"\t"); }
                b'r'  => { sys_write(1, b"\r"); }
                b'\\' => { sys_write(1, b"\\"); }
                other => { sys_write(1, b"\\"); sys_write(1, &[other]); }
            };
        } else {
            sys_write(1, &b[i..i+1]);
        }
        i += 1;
    }
}

unsafe fn collect_args(argc: i32, argv: *const *const u8) -> alloc::vec::Vec<alloc::string::String> {
    let mut out = alloc::vec::Vec::new();
    for i in 0..argc as usize {
        let p = *argv.add(i); if p.is_null() { break; }
        let mut len = 0; while *p.add(len) != 0 { len += 1; }
        let s = core::str::from_utf8(core::slice::from_raw_parts(p, len)).unwrap_or("");
        out.push(alloc::string::String::from(s));
    }
    out
}
