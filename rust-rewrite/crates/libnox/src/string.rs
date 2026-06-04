//! String utilities (for use without std).

pub fn strlen(s: &[u8]) -> usize {
    s.iter().position(|&b| b == 0).unwrap_or(s.len())
}

/// Parse integer from ASCII bytes.
pub fn atoi(s: &str) -> i64 {
    let s = s.trim_ascii();
    let (neg, s) = if s.starts_with('-') { (true, &s[1..]) } else { (false, s) };
    let mut n: i64 = 0;
    for b in s.bytes() {
        if b < b'0' || b > b'9' { break; }
        n = n * 10 + (b - b'0') as i64;
    }
    if neg { -n } else { n }
}

pub fn itoa(mut n: i64, buf: &mut [u8]) -> &str {
    if buf.is_empty() { return ""; }
    let neg = n < 0;
    if neg { n = -n; }
    let mut i = buf.len();
    loop {
        i -= 1;
        buf[i] = b'0' + (n % 10) as u8;
        n /= 10;
        if n == 0 { break; }
        if i == 0 { break; }
    }
    if neg && i > 0 { i -= 1; buf[i] = b'-'; }
    core::str::from_utf8(&buf[i..]).unwrap_or("")
}

pub fn split_once<'a>(s: &'a str, delim: char) -> Option<(&'a str, &'a str)> {
    let pos = s.find(delim)?;
    Some((&s[..pos], &s[pos+1..]))
}

pub fn trim(s: &str) -> &str { s.trim_ascii() }
