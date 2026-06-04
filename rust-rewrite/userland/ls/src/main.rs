#![no_std]
#![no_main]
extern crate alloc;
extern crate libnox;

use alloc::string::String;
use libnox::*;

#[no_mangle]
pub extern "C" fn main(_argc: i32, _argv: *const *const u8) -> i32 {
    let path = getcwd();
    let entries = listdir(&path);
    for name in &entries {
        println!("{}", name);
    }
    0
}
