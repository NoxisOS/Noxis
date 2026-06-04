//! Syscall dispatch table and individual handlers.

use abi::syscall::Syscall;
use abi::error::KError;
use sched::scheduler::current_pid;
use sched::process::with_procs;
use alloc::string::String;
use core::slice;
use core::str;

/// Main dispatch function — called from syscall_entry.
/// Returns i64: ≥0 = success, <0 = -errno.
#[no_mangle]
pub extern "C" fn dispatch(
    num: u64,
    a0: u64, a1: u64, a2: u64, a3: u64, a4: u64, a5: u64,
) -> i64 {
    match Syscall::from_u64(num) {
        Some(Syscall::Read)    => sys_read(a0, a1, a2),
        Some(Syscall::Write)   => sys_write(a0, a1, a2),
        Some(Syscall::Exit)    => sys_exit(a0 as i32),
        Some(Syscall::Getpid)  => current_pid() as i64,
        Some(Syscall::Sleep)   => sys_sleep(a0),
        Some(Syscall::Uptime)  => sys_uptime(),
        Some(Syscall::Brk)     => sys_brk(a0),
        Some(Syscall::Getcwd)  => sys_getcwd(a0, a1),
        Some(Syscall::Chdir)   => sys_chdir(a0),
        Some(Syscall::Stat)    => sys_stat(a0, a1),
        Some(Syscall::Open)    => sys_open(a0, a1),
        Some(Syscall::Close)   => sys_close(a0),
        Some(Syscall::Mkdir)   => sys_mkdir(a0, a1),
        Some(Syscall::Unlink)  => sys_unlink(a0),
        Some(Syscall::Rename)  => sys_rename(a0, a1),
        Some(Syscall::Getdents)=> sys_getdents(a0, a1, a2),
        Some(Syscall::Pipe)    => sys_pipe(a0),
        Some(Syscall::Dup2)    => sys_dup2(a0, a1),
        Some(Syscall::Getenv)  => sys_getenv(a0, a1, a2),
        Some(Syscall::Setenv)  => sys_setenv(a0, a1),
        Some(Syscall::Unsetenv)=> sys_unsetenv(a0),
        Some(Syscall::GetenvAt)=> sys_getenv_at(a0, a1, a2, a3),
        Some(Syscall::Kill)    => sys_kill(a0, a1),
        Some(Syscall::Fork)    => KError::NoSys.as_i64(), // TODO P9
        Some(Syscall::Exec)    => KError::NoSys.as_i64(), // TODO P9
        Some(Syscall::Waitpid) => KError::NoSys.as_i64(), // TODO P9
        _                      => KError::NoSys.as_i64(),
    }
}

// ── Helpers ───────────────────────────────────────────────────────────────────

fn user_str(ptr: u64, max: usize) -> Option<&'static str> {
    if ptr == 0 { return None; }
    // SAFETY: trusting the user pointer — will be validated with VMM in future
    let bytes = unsafe { slice::from_raw_parts(ptr as *const u8, max) };
    let end = bytes.iter().position(|&b| b == 0).unwrap_or(max);
    str::from_utf8(&bytes[..end]).ok()
}

fn user_bytes(ptr: u64, len: u64) -> &'static mut [u8] {
    unsafe { slice::from_raw_parts_mut(ptr as *mut u8, len as usize) }
}

// ── I/O ───────────────────────────────────────────────────────────────────────

fn sys_read(fd: u64, buf: u64, len: u64) -> i64 {
    let buf = user_bytes(buf, len);
    let pid = current_pid();
    with_procs(|t| {
        let p = t.get(&pid)?;
        let fd_obj = p.fds.get(fd as usize)?.0.clone();
        drop(t); // release lock before calling read
        Some(fd_obj.read(buf).unwrap_or_else(|e| e as usize as i64 as usize) as i64)
    }).unwrap_or(KError::BadFd.as_i64())
}

fn sys_write(fd: u64, buf: u64, len: u64) -> i64 {
    let buf = user_bytes(buf, len);
    let pid = current_pid();
    with_procs(|t| {
        let p = t.get(&pid)?;
        let fd_obj = p.fds.get(fd as usize)?.0.clone();
        drop(t);
        Some(fd_obj.write(buf).unwrap_or_else(|e| e as usize as i64 as usize) as i64)
    }).unwrap_or(KError::BadFd.as_i64())
}

// ── Process ───────────────────────────────────────────────────────────────────

fn sys_exit(code: i32) -> i64 {
    let pid = current_pid();
    with_procs(|t| {
        if let Some(p) = t.get_mut(&pid) {
            use sched::process::ProcessState;
            p.state = ProcessState::Zombie(code);
        }
    });
    sched::scheduler::schedule();
    0
}

fn sys_sleep(ms: u64) -> i64 {
    sched::scheduler::sleep_ms(ms);
    0
}

fn sys_uptime() -> i64 {
    sched::pit_ticks() as i64
}

fn sys_brk(new_brk: u64) -> i64 {
    let pid = current_pid();
    with_procs(|t| {
        if let Some(p) = t.get_mut(&pid) {
            if new_brk == 0 { return p.addr.brk as i64; }
            // Map pages between old brk and new brk
            use mm::vmm::{map_page, MapFlags};
            use x86_64::VirtAddr;
            let old = (p.addr.brk + 0xFFF) & !0xFFF;
            let new = (new_brk  + 0xFFF) & !0xFFF;
            let flags = MapFlags::PRESENT | MapFlags::WRITABLE | MapFlags::USER;
            let mut addr = old;
            while addr < new {
                let _ = map_page(VirtAddr::new(addr), None, flags);
                addr += 0x1000;
            }
            p.addr.brk = new_brk;
            new_brk as i64
        } else { KError::Inval.as_i64() }
    })
}

// ── Filesystem ────────────────────────────────────────────────────────────────

fn sys_open(path_ptr: u64, flags: u64) -> i64 {
    let path = match user_str(path_ptr, 512) {
        Some(s) => s,
        None    => return KError::Inval.as_i64(),
    };
    let pid = current_pid();
    let inode = vfs::vfs::with_vfs(|vfs| vfs.resolve(path));
    match inode {
        Ok(node) => {
            use alloc::sync::Arc;
            let fd_obj = Arc::new(vfs::inode::OpenFileFd::new(node, flags as u32));
            with_procs(|t| {
                if let Some(p) = t.get_mut(&pid) {
                    p.fds.insert(fd_obj) as i64
                } else { KError::Inval.as_i64() }
            })
        }
        Err(e) => e.as_i64(),
    }
}

fn sys_close(fd: u64) -> i64 {
    let pid = current_pid();
    with_procs(|t| {
        if let Some(p) = t.get_mut(&pid) {
            if p.fds.close(fd as usize) { 0 } else { KError::BadFd.as_i64() }
        } else { KError::Inval.as_i64() }
    })
}

fn sys_stat(path_ptr: u64, stat_ptr: u64) -> i64 {
    let path = match user_str(path_ptr, 512) { Some(s) => s, None => return KError::Inval.as_i64() };
    match vfs::vfs::with_vfs(|v| v.resolve(path)) {
        Ok(node) => match node.stat() {
            Ok(st) => {
                unsafe { *(stat_ptr as *mut abi::stat::Stat) = st; }
                0
            }
            Err(e) => e.as_i64(),
        }
        Err(e) => e.as_i64(),
    }
}

fn sys_mkdir(path_ptr: u64, mode: u64) -> i64 {
    let path = match user_str(path_ptr, 512) { Some(s) => s, None => return KError::Inval.as_i64() };
    match vfs::vfs::with_vfs(|v| v.resolve_parent(path)) {
        Ok((parent, name)) => parent.mkdir(&name, mode as u32).map(|_| 0).unwrap_or_else(|e| e.as_i64()),
        Err(e) => e.as_i64(),
    }
}

fn sys_unlink(path_ptr: u64) -> i64 {
    let path = match user_str(path_ptr, 512) { Some(s) => s, None => return KError::Inval.as_i64() };
    match vfs::vfs::with_vfs(|v| v.resolve_parent(path)) {
        Ok((parent, name)) => parent.unlink(&name).map(|_| 0).unwrap_or_else(|e| e.as_i64()),
        Err(e) => e.as_i64(),
    }
}

fn sys_rename(old_ptr: u64, new_ptr: u64) -> i64 {
    let old = match user_str(old_ptr, 512) { Some(s) => s, None => return KError::Inval.as_i64() };
    let new = match user_str(new_ptr, 512) { Some(s) => s, None => return KError::Inval.as_i64() };
    match vfs::vfs::with_vfs(|v| v.resolve_parent(old)) {
        Ok((parent, name)) => parent.rename(&name, new).map(|_| 0).unwrap_or_else(|e| e.as_i64()),
        Err(e) => e.as_i64(),
    }
}

fn sys_getdents(fd: u64, buf_ptr: u64, count: u64) -> i64 {
    // For simplicity: read dirents from fd's inode
    let pid = current_pid();
    with_procs(|t| {
        let p = t.get(&pid)?;
        let _fd_obj = p.fds.get(fd as usize)?;
        Some(KError::NoSys.as_i64()) // TODO: store offset in OpenFile
    }).unwrap_or(KError::BadFd.as_i64())
}

fn sys_pipe(fds_ptr: u64) -> i64 {
    let pid = current_pid();
    let (read_end, write_end) = vfs::pipe::new_pipe();
    with_procs(|t| {
        if let Some(p) = t.get_mut(&pid) {
            let rfd = p.fds.insert(read_end);
            let wfd = p.fds.insert(write_end);
            let ptr = fds_ptr as *mut [u32; 2];
            unsafe { (*ptr)[0] = rfd as u32; (*ptr)[1] = wfd as u32; }
            0i64
        } else { KError::Inval.as_i64() }
    })
}

fn sys_dup2(old_fd: u64, new_fd: u64) -> i64 {
    let pid = current_pid();
    with_procs(|t| {
        if let Some(p) = t.get_mut(&pid) {
            if let Some(fd) = p.fds.get(old_fd as usize).cloned() {
                p.fds.insert_at(new_fd as usize, fd.0.clone());
                new_fd as i64
            } else { KError::BadFd.as_i64() }
        } else { KError::Inval.as_i64() }
    })
}

// ── CWD ───────────────────────────────────────────────────────────────────────

fn sys_getcwd(buf: u64, size: u64) -> i64 {
    let pid = current_pid();
    with_procs(|t| {
        if let Some(p) = t.get(&pid) {
            let cwd = p.cwd.as_bytes();
            let n = cwd.len().min(size as usize - 1);
            unsafe {
                core::ptr::copy_nonoverlapping(cwd.as_ptr(), buf as *mut u8, n);
                *((buf + n as u64) as *mut u8) = 0;
            }
            0i64
        } else { KError::Inval.as_i64() }
    })
}

fn sys_chdir(path_ptr: u64) -> i64 {
    let path = match user_str(path_ptr, 512) { Some(s) => s, None => return KError::Inval.as_i64() };
    // Verify path exists and is a dir
    match vfs::vfs::with_vfs(|v| v.resolve(path)) {
        Ok(node) => match node.stat() {
            Ok(st) if abi::stat::s_isdir(st.mode) => {
                let pid = current_pid();
                with_procs(|t| {
                    if let Some(p) = t.get_mut(&pid) { p.cwd = String::from(path); }
                });
                0
            }
            Ok(_)  => KError::NotDir.as_i64(),
            Err(e) => e.as_i64(),
        }
        Err(e) => e.as_i64(),
    }
}

// ── Env ───────────────────────────────────────────────────────────────────────

fn sys_getenv(key_ptr: u64, buf: u64, size: u64) -> i64 {
    let key = match user_str(key_ptr, 256) { Some(s) => s, None => return KError::Inval.as_i64() };
    let pid = current_pid();
    with_procs(|t| {
        if let Some(p) = t.get(&pid) {
            if let Some(val) = p.env.get(key) {
                let v = val.as_bytes();
                let n = v.len().min(size as usize - 1);
                unsafe {
                    core::ptr::copy_nonoverlapping(v.as_ptr(), buf as *mut u8, n);
                    *((buf + n as u64) as *mut u8) = 0;
                }
                0i64
            } else { KError::NoEnt.as_i64() }
        } else { KError::Inval.as_i64() }
    })
}

fn sys_setenv(key_ptr: u64, val_ptr: u64) -> i64 {
    let key = match user_str(key_ptr, 256) { Some(s) => s, None => return KError::Inval.as_i64() };
    let val = match user_str(val_ptr, 1024){ Some(s) => s, None => return KError::Inval.as_i64() };
    let pid = current_pid();
    with_procs(|t| {
        if let Some(p) = t.get_mut(&pid) {
            p.env.insert(String::from(key), String::from(val));
            0i64
        } else { KError::Inval.as_i64() }
    })
}

fn sys_unsetenv(key_ptr: u64) -> i64 {
    let key = match user_str(key_ptr, 256) { Some(s) => s, None => return KError::Inval.as_i64() };
    let pid = current_pid();
    with_procs(|t| {
        if let Some(p) = t.get_mut(&pid) {
            p.env.remove(key);
            0i64
        } else { KError::Inval.as_i64() }
    })
}

fn sys_getenv_at(idx: u64, key_buf: u64, key_sz: u64, val_buf: u64) -> i64 {
    let pid = current_pid();
    with_procs(|t| {
        if let Some(p) = t.get(&pid) {
            if let Some((k, v)) = p.env.iter().nth(idx as usize) {
                let kb = k.as_bytes();
                let vb = v.as_bytes();
                let kn = kb.len().min(key_sz as usize - 1);
                unsafe {
                    core::ptr::copy_nonoverlapping(kb.as_ptr(), key_buf as *mut u8, kn);
                    *((key_buf + kn as u64) as *mut u8) = 0;
                    // value follows key_buf + key_sz
                    let vp = val_buf as *mut u8;
                    core::ptr::copy_nonoverlapping(vb.as_ptr(), vp, vb.len().min(255));
                    *vp.add(vb.len().min(255)) = 0;
                }
                0i64
            } else { KError::Range.as_i64() }
        } else { KError::Inval.as_i64() }
    })
}

// ── Signals ───────────────────────────────────────────────────────────────────

fn sys_kill(pid: u64, sig: u64) -> i64 {
    with_procs(|t| {
        if let Some(p) = t.get(&(pid as u32)) {
            p.pending.send(sig);
            0i64
        } else { KError::NoEnt.as_i64() }
    })
}
