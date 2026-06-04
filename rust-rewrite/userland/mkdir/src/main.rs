#![no_std]
#![no_main]
extern crate alloc;
extern crate libnox;
use libnox::*;

#[no_mangle]
pub extern "C" fn main(argc: i32, argv: *const *const u8) -> i32 {
    let args = unsafe { crate::collect_args(argc, argv) };
    if args.len() < 2 { eprintln!("usage: mkdir <dir>"); return 1; }
    for path in &args[1..] {
        if sys_mkdir(path, 0o755) < 0 {
            eprintln!("mkdir: cannot create '{}'", path);
        }
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
