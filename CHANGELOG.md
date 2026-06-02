# Changelog

All notable changes to Noxis OS are documented here.  
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

---

## [Unreleased]

### Added
- `unlink` / `rename` syscalls with full NoxFS dir entry removal
- `rm`, `cp`, `mv` builtins in nsh
- Tab completion in nsh (raw TTY mode)
- Kernel panic: stack trace + CR2 + freeze with `cli; hlt`
- SIGSEGV delivered via signal system instead of direct `proc_terminate`
- `waitpid` WNOHANG flag
- `noxfs_creat_path` — creates parent directories automatically
- `/etc/keymap.cfg` — keyboard layout persisted under `/etc/` instead of root

### Changed
- Removed VGA ANSI parser — VGA writes bytes raw (CP437 direct)
- `vga_put_char` simplified to pure `_raw_putc`
- `noxfs_unlink` refuses to remove non-empty directories
- `noxfs_rename` adds destination before removing source (safer)
- `_split_path` enforces 23-char basename limit (NoxFS dirent field size)
- Stack trace validates EBP range before dereference (no double-fault risk)

### Fixed
- `_ansi_colors` table: duplicate entry 95, missing 96 (LIGHT_CYAN)
- `vga_ansi_write` double-increment bug in main loop
- `nsh` banner: replaced UTF-8 box-drawing chars with CP437 hex literals
- `ATTR_TITLE` / `ATTR_NORMAL` were identical in panic screen
- `_cur_attr` not restored on early break in stack trace loop

---

## [0.8.0] — 2026-06-01

### Added
- **Copy-on-write fork** — pages shared read-only via PAGE_COW bit,
  per-frame reference counts in PMM, private copy on first write
- **SynFS** — synthetic filesystem backend (`/proc`, `/dev`)
  - `/proc/meminfo`, `/proc/slab`, `/proc/sched`, `/proc/uptime`
  - `/proc/memmap` — physical memory heatmap
  - `/dev/null`, `/dev/zero`, `/dev/random`, `/dev/keymap`
- **NoxFS v2** — inode-based on-disk filesystem with `mkdir`, `readdir`,
  `stat`, subdirectory support
- **Signals** — `sigaction`, `kill`, `sigreturn`, `sigprocmask`,
  `SIGINT`/`SIGTERM`/`SIGKILL`/`SIGSEGV` default actions
- **Pipes** — anonymous, blocking, refcounted, inherited by `fork`
- **Slab allocator** — typed caches for `process_t` and `pipe_t`
- **Per-process arenas** — bump allocator for `execve` argv copies
- **Tagged heap allocations** — per-subsystem breakdown via `memstat`
- `sysenter`/`sysexit` fast path in addition to `int 0x80`
- `dup2`, `lseek`, `sleep`, `time`, `getppid`, `getuid` syscalls
- `sigprocmask`, `raise()` in noxlib
- nsh: pipelines, redirections, background jobs, `$?` expansion

### Changed
- Process lifecycle fully closed — `proc_destroy` reclaims everything
- `execve` argv copied into per-process arena (off kernel stack)
- Keyboard layout persisted across reboots via `/dev/keymap` write

---

## [0.1.0] — 2026-05-15

### Added
- Custom two-stage MBR bootloader (no GRUB)
- Real mode → protected mode transition, A20 enable
- Higher-half kernel mapping (`0xC0000000`)
- GDT, IDT, PIC remapping, PIT at 1 kHz
- PMM bitmap allocator
- Two-level page directory/table VMM
- First-fit kernel heap (`kmalloc`/`kfree`)
- Demand paging (stack and heap growth via `#PF`)
- Round-robin preemptive scheduler
- `fork` + `execve` + `exit` + `waitpid`
- ELF32 loader
- Ring-3 user mode
- VGA 80×25, PS/2 keyboard, UART serial, ATA PIO
- TTY (canonical mode, echo, Ctrl+C/D)
- RamFS + basic VFS
- noxlib: `crt0`, `printf`, `malloc`, `string.h`
- nsh: basic REPL with `cd`, `ls`, `cat`, `echo`
