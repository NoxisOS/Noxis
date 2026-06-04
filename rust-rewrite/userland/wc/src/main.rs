#![no_std]
#![no_main]
extern crate alloc;
extern crate libnox;
use libnox::*;

#[no_mangle]
pub extern "C" fn main(argc: i32, argv: *const *const u8) -> i32 {
    let args = unsafe { crate::collect_args(argc, argv) };
    let stdin = args.len() < 2;
    let files: alloc::vec::Vec<_> = if stdin {
        alloc::vec![alloc::string::String::from("-")]
    } else {
        args[1..].to_vec()
    };
    for path in &files {
        let data = if path == "-" {
            let mut v = alloc::vec::Vec::new();
            let mut buf = [0u8; 512];
            loop { let n = sys_read(0, &mut buf); if n <= 0 { break; } v.extend_from_slice(&buf[..n as usize]); }
            v
        } else {
            libnox::fs::read_file(path).unwrap_or_default()
        };
        let lines = data.iter().filter(|&&b| b == b'\n').count();
        let words = data.split(|b| *b == b' ' || *b == b'\t' || *b == b'\n').filter(|s| !s.is_empty()).count();
        let bytes = data.len();
        println!("{:>7} {:>7} {:>7} {}", lines, words, bytes, if path == "-" { "" } else { path });
    }
    0
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
