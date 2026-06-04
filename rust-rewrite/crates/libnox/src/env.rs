//! Environment variable helpers.

use alloc::string::String;
use crate::syscall::{sys_getenv, sys_setenv, sys_unsetenv, sys_getenv_at};

pub fn getenv(key: &str) -> Option<String> {
    let mut buf = [0u8; 1024];
    if sys_getenv(key, &mut buf) == 0 {
        let s = core::str::from_utf8(&buf).ok()?;
        let end = s.find('\0').unwrap_or(s.len());
        Some(String::from(&s[..end]))
    } else {
        None
    }
}

pub fn setenv(key: &str, val: &str) { sys_setenv(key, val); }
pub fn unsetenv(key: &str)          { sys_unsetenv(key); }

/// Iterate over all environment entries.
pub fn env_iter() -> impl Iterator<Item = (String, String)> {
    (0..).map_while(|i| {
        let mut key = [0u8; 256];
        let mut val = [0u8; 1024];
        if sys_getenv_at(i, &mut key, &mut val) < 0 { return None; }
        let k = String::from(cstr_to_str(&key));
        let v = String::from(cstr_to_str(&val));
        Some((k, v))
    })
}

fn cstr_to_str(buf: &[u8]) -> &str {
    let end = buf.iter().position(|&b| b == 0).unwrap_or(buf.len());
    core::str::from_utf8(&buf[..end]).unwrap_or("")
}
