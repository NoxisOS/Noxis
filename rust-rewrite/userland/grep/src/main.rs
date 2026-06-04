#![no_std]
#![no_main]
extern crate alloc;
extern crate libnox;
use libnox::*;
use alloc::string::String;

#[no_mangle]
pub extern "C" fn main(argc: i32, argv: *const *const u8) -> i32 {
    let args = unsafe { crate::collect_args(argc, argv) };
    if args.len() < 2 { eprintln!("usage: grep <pattern> [file...]"); return 1; }
    let pattern = &args[1];
    let mut found = false;

    let files: alloc::vec::Vec<String> = if args.len() < 3 {
        alloc::vec![String::from("-")]
    } else {
        args[2..].to_vec()
    };

    for path in &files {
        let data = if path == "-" {
            let mut v = alloc::vec::Vec::new();
            let mut buf = [0u8; 512];
            loop { let n = sys_read(0, &mut buf); if n <= 0 { break; } v.extend_from_slice(&buf[..n as usize]); }
            v
        } else {
            match libnox::fs::read_file(path) {
                Some(d) => d,
                None => { eprintln!("grep: {}: not found", path); continue; }
            }
        };
        let s = core::str::from_utf8(&data).unwrap_or("");
        for line in s.lines() {
            if line.contains(pattern.as_str()) {
                if files.len() > 1 { print!("{}:", path); }
                println!("{}", line);
                found = true;
            }
        }
    }
    if found { 0 } else { 1 }
}
unsafe fn collect_args(argc: i32, argv: *const *const u8) -> alloc::vec::Vec<alloc::string::String> {
    let mut out = alloc::vec::Vec::new();
    for i in 0..argc as usize {
        let p = *argv.add(i); if p.is_null() { break; }
        let mut len = 0; while *p.add(len) != 0 { len += 1; }
        out.push(alloc::string::String::from(core::str::from_utf8(core::slice::from_raw_parts(p, len)).unwrap_or("")));
    }
    out
}
