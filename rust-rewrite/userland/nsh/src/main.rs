#![no_std]
#![no_main]
extern crate alloc;
extern crate libnox;

use alloc::{string::String, vec::Vec, format};
use libnox::*;
use libnox::readline::Readline;

#[no_mangle]
pub extern "C" fn main(_argc: i32, _argv: *const *const u8) -> i32 {
    // Set default environment
    sys_setenv("PATH", "/bin:/");
    sys_setenv("HOME", "/");
    sys_setenv("SHELL", "nsh");
    sys_setenv("PS1", "\\w $ ");

    println!("\x1b[32mNoxis Shell (nsh)\x1b[0m  —  type 'help' for commands");

    let mut rl = Readline::new();
    loop {
        let cwd = getcwd();
        let prompt = format!("\x1b[36m{}\x1b[0m $ ", cwd);
        match rl.read(&prompt) {
            None        => { println!("exit"); break; }
            Some(line)  => {
                let line = String::from(line.trim());
                if line.is_empty() { continue; }
                let status = exec_line(&line);
                if status == 999 { break; } // exit builtin
            }
        }
    }
    0
}

// ── Line executor ─────────────────────────────────────────────────────────────

fn exec_line(line: &str) -> i32 {
    // Handle pipelines: cmd1 | cmd2 | cmd3
    let stages: Vec<&str> = line.splitn(8, '|').map(str::trim).collect();
    if stages.len() == 1 {
        exec_cmd(stages[0])
    } else {
        run_pipeline(&stages)
    }
}

fn run_pipeline(stages: &[&str]) -> i32 {
    // Set up pipes between stages
    let n = stages.len();
    let mut pipes: Vec<[i32; 2]> = Vec::new();
    for _ in 0..n-1 {
        let mut p = [0i32; 2];
        sys_pipe(&mut p);
        pipes.push(p);
    }

    let mut pids = Vec::new();
    for (i, &stage) in stages.iter().enumerate() {
        let pid = sys_fork();
        if pid == 0 {
            // Child: wire up stdin/stdout
            if i > 0 { sys_dup2(pipes[i-1][0], 0); }
            if i < n-1 { sys_dup2(pipes[i][1], 1); }
            // Close all pipe fds
            for p in &pipes { sys_close(p[0]); sys_close(p[1]); }
            sys_exit(exec_cmd(stage));
        }
        pids.push(pid);
    }

    // Close all pipes in parent
    for p in &pipes { sys_close(p[0]); sys_close(p[1]); }

    let mut status = 0i32;
    for pid in pids { sys_waitpid(pid, &mut status); }
    status
}

fn exec_cmd(raw: &str) -> i32 {
    // Variable expansion
    let expanded = expand_vars(raw);
    let tokens = tokenize(&expanded);
    if tokens.is_empty() { return 0; }

    // Handle redirections
    let (cmd_tokens, stdin_file, stdout_file, append) = parse_redirs(tokens);
    if cmd_tokens.is_empty() { return 0; }

    let cmd = &cmd_tokens[0];
    let args: Vec<&str> = cmd_tokens.iter().map(String::as_str).collect();

    // Apply redirections if needed (fork first)
    let need_fork = stdin_file.is_some() || stdout_file.is_some();

    if need_fork {
        let pid = sys_fork();
        if pid == 0 {
            if let Some(path) = &stdin_file {
                let fd = sys_open(path, 0);
                if fd >= 0 { sys_dup2(fd, 0); sys_close(fd); }
            }
            if let Some(path) = &stdout_file {
                let flags = if append { 0x401 } else { 0x201 }; // O_APPEND|O_CREAT or O_CREAT
                let fd = sys_open(path, flags);
                if fd >= 0 { sys_dup2(fd, 1); sys_close(fd); }
            }
            sys_exit(run_cmd(cmd, &args));
        }
        let mut st = 0i32;
        sys_waitpid(pid, &mut st);
        return st;
    }

    run_cmd(cmd, &args)
}

fn run_cmd(cmd: &str, args: &[&str]) -> i32 {
    // Builtins
    match cmd {
        "exit"  => return 999,
        "cd"    => {
            let dir = args.get(1).copied().unwrap_or("/");
            if sys_chdir(dir) < 0 { eprintln!("cd: {}: not found", dir); }
            return 0;
        }
        "pwd"   => { println!("{}", getcwd()); return 0; }
        "export"=> {
            if let Some(a) = args.get(1) {
                if let Some((k, v)) = a.split_once('=') {
                    sys_setenv(k, v);
                }
            }
            return 0;
        }
        "env"   => {
            for (k, v) in env_iter() { println!("{}={}", k, v); }
            return 0;
        }
        "unset" => { if let Some(k) = args.get(1) { sys_unsetenv(k); } return 0; }
        "help"  => {
            println!("Builtins: cd pwd exit export env unset help");
            println!("Programs: ls cat echo ps mkdir rm cp wc grep");
            return 0;
        }
        "true"  => return 0,
        "false" => return 1,
        _ => {}
    }

    // External command
    let path = find_cmd(cmd);
    let pid = sys_fork();
    if pid < 0 { eprintln!("nsh: fork failed"); return 1; }
    if pid == 0 {
        sys_exec(&path, args);
        eprintln!("nsh: {}: command not found", cmd);
        sys_exit(127);
    }
    let mut status = 0i32;
    sys_waitpid(pid, &mut status);
    status
}

fn find_cmd(cmd: &str) -> String {
    if cmd.contains('/') { return String::from(cmd); }
    let path_env = getenv("PATH").unwrap_or_else(|| String::from("/"));
    for dir in path_env.split(':') {
        let full = format!("{}/{}", dir, cmd);
        if libnox::fs::stat(&full).is_some() { return full; }
    }
    String::from(cmd)
}

// ── Tokenizer ─────────────────────────────────────────────────────────────────

fn tokenize(s: &str) -> Vec<String> {
    let mut tokens = Vec::new();
    let mut cur = String::new();
    let mut in_single = false;
    let mut in_double = false;
    let mut chars = s.chars().peekable();

    while let Some(c) = chars.next() {
        match c {
            '\'' if !in_double => { in_single = !in_single; }
            '"'  if !in_single => { in_double = !in_double; }
            ' ' | '\t' if !in_single && !in_double => {
                if !cur.is_empty() { tokens.push(cur.clone()); cur.clear(); }
            }
            _ => cur.push(c),
        }
    }
    if !cur.is_empty() { tokens.push(cur); }
    tokens
}

fn parse_redirs(tokens: Vec<String>) -> (Vec<String>, Option<String>, Option<String>, bool) {
    let mut cmd = Vec::new();
    let mut stdin_f  = None;
    let mut stdout_f = None;
    let mut append   = false;
    let mut i = 0;
    while i < tokens.len() {
        match tokens[i].as_str() {
            "<"  => { stdin_f = tokens.get(i+1).cloned(); i += 2; }
            ">>" => { stdout_f = tokens.get(i+1).cloned(); append = true; i += 2; }
            ">"  => { stdout_f = tokens.get(i+1).cloned(); i += 2; }
            _    => { cmd.push(tokens[i].clone()); i += 1; }
        }
    }
    (cmd, stdin_f, stdout_f, append)
}

// ── Variable expansion ────────────────────────────────────────────────────────

fn expand_vars(s: &str) -> String {
    let mut out = String::new();
    let mut chars = s.chars().peekable();
    while let Some(c) = chars.next() {
        if c == '$' {
            if chars.peek() == Some(&'(') {
                chars.next(); // consume (
                // Command substitution $(...)
                let mut inner = String::new();
                let mut depth = 1;
                for ch in chars.by_ref() {
                    if ch == '(' { depth += 1; }
                    if ch == ')' { depth -= 1; if depth == 0 { break; } }
                    inner.push(ch);
                }
                out.push_str(&cmd_subst(&inner));
            } else {
                let mut key = String::new();
                while let Some(&ch) = chars.peek() {
                    if ch.is_alphanumeric() || ch == '_' { key.push(ch); chars.next(); }
                    else { break; }
                }
                if let Some(val) = getenv(&key) { out.push_str(&val); }
            }
        } else {
            out.push(c);
        }
    }
    out
}

fn cmd_subst(cmd: &str) -> String {
    // Fork + capture stdout
    let mut pipe = [0i32; 2];
    sys_pipe(&mut pipe);
    let pid = sys_fork();
    if pid == 0 {
        sys_close(pipe[0]);
        sys_dup2(pipe[1], 1);
        sys_close(pipe[1]);
        sys_exit(exec_line(cmd));
    }
    sys_close(pipe[1]);
    let mut out = alloc::vec::Vec::new();
    let mut buf = [0u8; 256];
    loop {
        let n = sys_read(pipe[0], &mut buf);
        if n <= 0 { break; }
        out.extend_from_slice(&buf[..n as usize]);
    }
    sys_close(pipe[0]);
    let mut st = 0;
    sys_waitpid(pid, &mut st);
    // Trim trailing newlines
    while out.last() == Some(&b'\n') { out.pop(); }
    String::from(core::str::from_utf8(&out).unwrap_or(""))
}
