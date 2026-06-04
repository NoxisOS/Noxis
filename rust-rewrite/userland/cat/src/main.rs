#![no_std]
#![no_main]
extern crate alloc;
extern crate libnox;

use libnox::*;

#[no_mangle]
pub extern "C" fn main(argc: i32, argv: *const *const u8) -> i32 {
    let args = unsafe { collect_args(argc, argv) };
    if args.len() < 2 {
        // Read stdin
        let mut buf = [0u8; 512];
        loop {
            let n = sys_read(0, &mut buf);
            if n <= 0 { break; }
            sys_write(1, &buf[..n as usize]);
        }
        return 0;
    }
    for path in &args[1..] {
        let fd = sys_open(path, 0);
        if fd < 0 { eprintln!("cat: {}: not found", path); continue; }
        let mut buf = [0u8; 512];
        loop {
            let n = sys_read(fd, &mut buf);
            if n <= 0 { break; }
            sys_write(1, &buf[..n as usize]);
        }
        sys_close(fd);
    }
    0
}

unsafe fn collect_args(argc: i32, argv: *const *const u8) -> alloc::vec::Vec<alloc::string::String> {
    let mut out = alloc::vec::Vec::new();
    for i in 0..argc as usize {
        let p = *argv.add(i);
        if p.is_null() { break; }
        let mut len = 0;
        while *p.add(len) != 0 { len += 1; }
        let s = core::str::from_utf8(core::slice::from_raw_parts(p, len)).unwrap_or("");
        out.push(alloc::string::String::from(s));
    }
    out
}
