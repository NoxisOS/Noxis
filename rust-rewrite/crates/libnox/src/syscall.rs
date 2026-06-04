//! Raw SYSCALL wrappers — inline asm, SysV ABI.

use abi::syscall::Syscall;

#[inline(always)]
pub unsafe fn syscall0(n: u64) -> i64 {
    let ret: i64;
    core::arch::asm!("syscall", inlateout("rax") n as i64 => ret,
        out("rcx") _, out("r11") _, options(nostack));
    ret
}
#[inline(always)]
pub unsafe fn syscall1(n: u64, a0: u64) -> i64 {
    let ret: i64;
    core::arch::asm!("syscall", inlateout("rax") n as i64 => ret,
        in("rdi") a0, out("rcx") _, out("r11") _, options(nostack));
    ret
}
#[inline(always)]
pub unsafe fn syscall2(n: u64, a0: u64, a1: u64) -> i64 {
    let ret: i64;
    core::arch::asm!("syscall", inlateout("rax") n as i64 => ret,
        in("rdi") a0, in("rsi") a1, out("rcx") _, out("r11") _, options(nostack));
    ret
}
#[inline(always)]
pub unsafe fn syscall3(n: u64, a0: u64, a1: u64, a2: u64) -> i64 {
    let ret: i64;
    core::arch::asm!("syscall", inlateout("rax") n as i64 => ret,
        in("rdi") a0, in("rsi") a1, in("rdx") a2,
        out("rcx") _, out("r11") _, options(nostack));
    ret
}
#[inline(always)]
pub unsafe fn syscall4(n: u64, a0: u64, a1: u64, a2: u64, a3: u64) -> i64 {
    let ret: i64;
    core::arch::asm!("syscall", inlateout("rax") n as i64 => ret,
        in("rdi") a0, in("rsi") a1, in("rdx") a2, in("r10") a3,
        out("rcx") _, out("r11") _, options(nostack));
    ret
}

// ── Typed wrappers ────────────────────────────────────────────────────────────

pub fn sys_read(fd: i32, buf: &mut [u8]) -> isize {
    unsafe { syscall3(Syscall::Read as u64, fd as u64, buf.as_mut_ptr() as u64, buf.len() as u64) as isize }
}
pub fn sys_write(fd: i32, buf: &[u8]) -> isize {
    unsafe { syscall3(Syscall::Write as u64, fd as u64, buf.as_ptr() as u64, buf.len() as u64) as isize }
}
pub fn sys_exit(code: i32) -> ! {
    unsafe { syscall1(Syscall::Exit as u64, code as u64); }
    loop {}
}
pub fn sys_getpid() -> u32 {
    unsafe { syscall0(Syscall::Getpid as u64) as u32 }
}
pub fn sys_sleep(ms: u64) {
    unsafe { syscall1(Syscall::Sleep as u64, ms); }
}
pub fn sys_uptime() -> u64 {
    unsafe { syscall0(Syscall::Uptime as u64) as u64 }
}
pub fn sys_brk(addr: usize) -> usize {
    unsafe { syscall1(Syscall::Brk as u64, addr as u64) as usize }
}
pub fn sys_open(path: &str, flags: u32) -> i32 {
    let mut buf = [0u8; 512];
    let n = path.len().min(511);
    buf[..n].copy_from_slice(&path.as_bytes()[..n]);
    unsafe { syscall2(Syscall::Open as u64, buf.as_ptr() as u64, flags as u64) as i32 }
}
pub fn sys_close(fd: i32) -> i32 {
    unsafe { syscall1(Syscall::Close as u64, fd as u64) as i32 }
}
pub fn sys_stat(path: &str, st: &mut abi::stat::Stat) -> i32 {
    let mut buf = [0u8; 512];
    let n = path.len().min(511);
    buf[..n].copy_from_slice(&path.as_bytes()[..n]);
    unsafe { syscall2(Syscall::Stat as u64, buf.as_ptr() as u64, st as *mut _ as u64) as i32 }
}
pub fn sys_mkdir(path: &str, mode: u32) -> i32 {
    let mut buf = [0u8; 512];
    let n = path.len().min(511);
    buf[..n].copy_from_slice(&path.as_bytes()[..n]);
    unsafe { syscall2(Syscall::Mkdir as u64, buf.as_ptr() as u64, mode as u64) as i32 }
}
pub fn sys_unlink(path: &str) -> i32 {
    let mut buf = [0u8; 512];
    let n = path.len().min(511);
    buf[..n].copy_from_slice(&path.as_bytes()[..n]);
    unsafe { syscall1(Syscall::Unlink as u64, buf.as_ptr() as u64) as i32 }
}
pub fn sys_rename(old: &str, new: &str) -> i32 {
    let mut ob = [0u8; 512]; let mut nb = [0u8; 512];
    let on = old.len().min(511); ob[..on].copy_from_slice(&old.as_bytes()[..on]);
    let nn = new.len().min(511); nb[..nn].copy_from_slice(&new.as_bytes()[..nn]);
    unsafe { syscall2(Syscall::Rename as u64, ob.as_ptr() as u64, nb.as_ptr() as u64) as i32 }
}
pub fn sys_getcwd(buf: &mut [u8]) -> i32 {
    unsafe { syscall2(Syscall::Getcwd as u64, buf.as_mut_ptr() as u64, buf.len() as u64) as i32 }
}
pub fn sys_chdir(path: &str) -> i32 {
    let mut buf = [0u8; 512];
    let n = path.len().min(511);
    buf[..n].copy_from_slice(&path.as_bytes()[..n]);
    unsafe { syscall1(Syscall::Chdir as u64, buf.as_ptr() as u64) as i32 }
}
pub fn sys_getdents(fd: i32, buf: &mut [abi::dirent::Dirent]) -> i32 {
    unsafe { syscall3(Syscall::Getdents as u64, fd as u64, buf.as_mut_ptr() as u64, buf.len() as u64) as i32 }
}
pub fn sys_pipe(fds: &mut [i32; 2]) -> i32 {
    unsafe { syscall1(Syscall::Pipe as u64, fds.as_mut_ptr() as u64) as i32 }
}
pub fn sys_dup2(old: i32, new: i32) -> i32 {
    unsafe { syscall2(Syscall::Dup2 as u64, old as u64, new as u64) as i32 }
}
pub fn sys_fork() -> i32 {
    unsafe { syscall0(Syscall::Fork as u64) as i32 }
}
pub fn sys_exec(path: &str, argv: &[&str]) -> i32 {
    let mut pbuf = [0u8; 512];
    let n = path.len().min(511);
    pbuf[..n].copy_from_slice(&path.as_bytes()[..n]);
    // Pack argv as null-terminated strings
    let mut abuf = [0u8; 4096];
    let mut ptrs = [0u64; 64];
    let mut off = 0usize;
    let mut nargs = 0usize;
    for &arg in argv {
        ptrs[nargs] = abuf[off..].as_ptr() as u64;
        let an = arg.len().min(255);
        abuf[off..off+an].copy_from_slice(&arg.as_bytes()[..an]);
        off += an + 1; // null terminator
        nargs += 1;
        if nargs >= 63 { break; }
    }
    ptrs[nargs] = 0; // null-terminate argv array
    unsafe { syscall3(Syscall::Exec as u64, pbuf.as_ptr() as u64, ptrs.as_ptr() as u64, nargs as u64) as i32 }
}
pub fn sys_waitpid(pid: i32, status: &mut i32) -> i32 {
    unsafe { syscall2(Syscall::Waitpid as u64, pid as u64, status as *mut _ as u64) as i32 }
}
pub fn sys_kill(pid: u32, sig: u64) -> i32 {
    unsafe { syscall2(Syscall::Kill as u64, pid as u64, sig) as i32 }
}
pub fn sys_getenv(key: &str, buf: &mut [u8]) -> i32 {
    let mut kb = [0u8; 256];
    let n = key.len().min(255); kb[..n].copy_from_slice(&key.as_bytes()[..n]);
    unsafe { syscall3(Syscall::Getenv as u64, kb.as_ptr() as u64, buf.as_mut_ptr() as u64, buf.len() as u64) as i32 }
}
pub fn sys_setenv(key: &str, val: &str) -> i32 {
    let mut kb = [0u8; 256]; let mut vb = [0u8; 1024];
    let kn = key.len().min(255); kb[..kn].copy_from_slice(&key.as_bytes()[..kn]);
    let vn = val.len().min(1023); vb[..vn].copy_from_slice(&val.as_bytes()[..vn]);
    unsafe { syscall2(Syscall::Setenv as u64, kb.as_ptr() as u64, vb.as_ptr() as u64) as i32 }
}
pub fn sys_unsetenv(key: &str) -> i32 {
    let mut kb = [0u8; 256];
    let n = key.len().min(255); kb[..n].copy_from_slice(&key.as_bytes()[..n]);
    unsafe { syscall1(Syscall::Unsetenv as u64, kb.as_ptr() as u64) as i32 }
}
pub fn sys_getenv_at(idx: usize, key_buf: &mut [u8], val_buf: &mut [u8]) -> i32 {
    unsafe { syscall4(Syscall::GetenvAt as u64, idx as u64,
        key_buf.as_mut_ptr() as u64, key_buf.len() as u64,
        val_buf.as_mut_ptr() as u64) as i32 }
}

// Convenience: write without buffer allocation
pub unsafe fn raw_write(fd: i32, ptr: *const u8, len: usize) -> isize {
    syscall3(Syscall::Write as u64, fd as u64, ptr as u64, len as u64) as isize
}
