//! Process helpers: fork/exec/wait.

use crate::syscall::{sys_fork, sys_exec, sys_waitpid, sys_exit};

/// Fork + exec helper. Returns child PID or -errno.
pub fn spawn(path: &str, argv: &[&str]) -> i32 {
    let pid = sys_fork();
    if pid == 0 {
        // Child
        sys_exec(path, argv);
        sys_exit(127); // exec failed
    }
    pid
}

/// Wait for child and return exit status.
pub fn wait(pid: i32) -> i32 {
    let mut status = 0i32;
    sys_waitpid(pid, &mut status);
    status
}
