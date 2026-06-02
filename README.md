<div align="center">

<img src="docs/logo_noxis.png.png" alt="Noxis OS Logo" width="120" />

<br/>

<h1>Noxis OS</h1>
<p><strong>A 32-bit hobby kernel built from scratch</strong></p>

<p>
<a href="docs/ARCHITECTURE.md"><strong>Architecture</strong></a> ·
<a href="docs/MEMORY_LAYOUT.md"><strong>Memory Layout</strong></a> ·
<a href="docs/CONVENTIONS.md"><strong>Conventions</strong></a>
</p>

<div>
<img src="https://img.shields.io/badge/Language-C11%20%2B%20NASM-blue?style=for-the-badge&logo=c" alt="Language"/>
<img src="https://img.shields.io/badge/Architecture-x86%2032--bit-orange?style=for-the-badge&logo=intel" alt="Architecture"/>
<img src="https://img.shields.io/badge/Kernel-Monolithic-green?style=for-the-badge" alt="Kernel Type"/>
<img src="https://img.shields.io/badge/License-CC%20BY--NC%204.0-lightgrey?style=for-the-badge" alt="License"/>
</div>

</div>

---

> No external libraries. No GRUB. No shortcuts.  
> Every byte — from the MBR to the shell — written by hand.

---

## ⚡ Quick Start

Requires an `i686-elf` cross-compiler. See [`.opencode/skills/cross-compiler.md`](.opencode/skills/cross-compiler.md).

```bash
make              # build kernel + userland
make run          # QEMU — VGA window + serial on stdout
make run-headless # QEMU — serial only, no window
make run-debug    # QEMU + GDB stub on :1234
```

---

## What is Noxis?

Noxis is a monolithic 32-bit operating system written entirely in **C11** and **NASM assembly** — no libc, no GRUB, no borrowed code.  
It boots from a custom two-stage MBR bootloader, transitions to protected mode, maps the kernel to the higher half, and launches a Unix-like shell as PID 1.

---

## Architecture

| Item | Value |
|---|---|
| Language | C11 (freestanding) + NASM x86 |
| Target | `i686-elf`, 32-bit protected mode + paging |
| Kernel type | Monolithic, strict layered (no upward deps) |
| Kernel base | `0xC0100000` (higher-half) |
| User space | `0x00400000` – `0xBFFFFFFF` (3 GB) |
| Kernel stack | 8 KB per process |
| Toolchain | `i686-elf-gcc` + `i686-elf-ld` |

---

## What's inside

<details open>
<summary><strong>🥾 Boot</strong></summary>

- Custom two-stage bootloader (MBR + Stage 2) in NASM — no GRUB
- A20 line enable, real mode → protected mode transition
- Kernel loaded at 1 MB, higher-half mapped to `0xC0000000`
- Serial boot log on COM1 (`-serial stdio`) for headless debugging

</details>

<details open>
<summary><strong>🔧 HAL</strong></summary>

- GDT with kernel/user segments + TSS for ring-0 stack switching
- IDT — 256 gates: exceptions (0–31), IRQs (32–47), syscall `int 0x80` (128)
- 8259A PIC remapped to IRQ 32–47
- PIT channel 0 at 1 kHz — `pit_uptime_ms()`, preemption clock
- x87 FPU with lazy context switching (`#NM` fault-driven save/restore)

</details>

<details open>
<summary><strong>🧠 Memory Management</strong></summary>

- **PMM** — bitmap allocator over all detected RAM
- **VMM** — two-level page directory/table, recursive mapping
- **Demand paging** — `#PF` handler grows user stack and heap on first touch
- **Copy-on-write fork** — pages shared read-only, private copy on first write
- **Kernel heap** — first-fit `kmalloc`/`kfree` with coalescing at `0xC0400000`
- **Slab caches** — typed pools for `process_t`, `pipe_t` — O(1) alloc/free, use-after-free detection (`0xDEADC0DE` poison)
- **Per-process arenas** — bump allocator tied to process lifetime, `O(1)` alloc, freed in one shot on exit
- **Tagged allocations** — every heap allocation attributed to a subsystem; `memstat` prints per-module breakdown

Live memory heatmap — `cat /proc/memmap` renders the PMM bitmap as a 64×16 colour grid:

```
dim grey = free   red = kernel   green = user   magenta = CoW shared
```

</details>

<details open>
<summary><strong>💽 Drivers</strong></summary>

- VGA 80×25 text mode — putchar, scrolling, 16 colours, CP437
- PS/2 keyboard — scancode → ASCII, buffered input
- 16550 UART serial (COM1) — polling TX, kernel debug output
- ATA PIO — sector read/write for the disk filesystem
- Generic block device layer + buffer cache
- **TTY** — canonical/raw mode, echo, line editing, Ctrl+C / Ctrl+D

</details>

<details open>
<summary><strong>📂 Filesystem</strong></summary>

- **VFS** — unified fd interface: `open` / `read` / `write` / `close` / `lseek` / `stat`
- **NoxFS v2** — inode-based on-disk FS with `mkdir`, `readdir`, `stat`, `unlink`, `rename`
- **SynFS** — synthetic procfs/devtmpfs-style backend, content generated live:

| Path | Content |
|---|---|
| `/proc/meminfo` | Physical + heap memory breakdown |
| `/proc/slab` | Slab cache live/free/peak counts |
| `/proc/sched` | Process table (pid, state, name) |
| `/proc/uptime` | Seconds since boot |
| `/proc/memmap` | Physical memory heatmap |
| `/dev/null` `/dev/zero` `/dev/random` | Classic devices |
| `/dev/keymap` | Write to switch keyboard layout |

- **Pipes** — anonymous, blocking, refcounted, inherited by `fork`

</details>

<details open>
<summary><strong>⚙️ Processes & Scheduling</strong></summary>

- `fork` — full address-space copy via `vmm_fork_pd` + CoW
- `execve` — load flat ELF32 into a fresh address space
- `exit` / `waitpid` — zombie collection, exit code, SIGCHLD, WNOHANG
- **Round-robin preemptive scheduler** — PIT IRQ0, configurable quantum
- Ring-3 user mode — `iret` into CPL=3 with isolated address space

</details>

<details open>
<summary><strong>📡 Signals</strong></summary>

| Signal | Status |
|---|---|
| `SIGINT` (Ctrl+C) | ✅ kills foreground child, shell survives |
| `SIGTERM` `SIGKILL` `SIGSEGV` `SIGILL` `SIGFPE` `SIGBUS` `SIGABRT` | ✅ default action (terminate) |
| `SIGCHLD` | ✅ sent to parent on child exit |
| Custom handlers via `signal()` / `sigaction()` | ✅ full save/restore via `sig_ucontext_t` |
| `sigreturn` trampoline | ✅ restores all registers + EFLAGS |
| `sigprocmask` | ✅ |
| Process groups / job control | ❌ not yet |

</details>

<details open>
<summary><strong>🔌 Syscall Interface</strong></summary>

Both `int 0x80` and `sysenter`/`sysexit` fast path supported — **30 syscalls**:

| # | Name | # | Name | # | Name |
|---|---|---|---|---|---|
| 0 | `exit` | 10 | `sigaction` | 20 | `brk` |
| 1 | `write` | 11 | `kill` | 21 | `getppid` |
| 2 | `read` | 12 | `getpid` | 22 | `getuid` |
| 3 | `open` | 13 | `ioctl` | 23 | `time` |
| 4 | `close` | 14 | `mkdir` | 24 | `dup2` |
| 5 | `fork` | 15 | `chdir` | 25 | `sleep` |
| 6 | `waitpid` | 16 | `getdents` | 26 | `sigreturn` |
| 7 | `creat` | 17 | `stat` | 27 | `sigprocmask` |
| 8 | `pipe` | 18 | `lseek` | 28 | `unlink` |
| 9 | `dup` | 19 | `execve` | 29 | `rename` |

</details>

<details open>
<summary><strong>🐚 nsh — The Shell</strong></summary>

There is no in-kernel shell. `nsh` is a ring-3 user process launched by the kernel as PID 1.

```
nsh / > ls
  /etc/
  /proc/
  /dev/
  nsh.elf
  ctest.elf

nsh / > cat /proc/sched
PID  STATE    NAME
1    running  nsh.elf

nsh / > echo hello | cat
hello
```

Features:
- Pipelines: `cmd1 | cmd2 | cmd3`
- Redirections: `>`, `>>`, `<`
- Background jobs: `cmd &`
- `$?` expansion
- Tab completion
- Builtins: `cd`, `pwd`, `ls`, `cat`, `rm`, `cp`, `mv`, `mkdir`, `echo`, `clear`, `keymap`, `exit`, `help`

</details>

<details open>
<summary><strong>📦 noxlib — User-space C Runtime</strong></summary>

A minimal libc written from scratch, linked into every userland ELF:

- `crt0.asm` — `_start` → `main()` → `exit()`
- `stdio.h` — `printf`, `snprintf`, `vprintf`, `puts`, `putchar`, `fgets`
- `stdlib.h` — `malloc`/`free`, `exit`, `atoi`, `strtol`
- `string.h` — `strlen`, `strcpy`, `strcmp`, `memcpy`, `memset`…
- `signal.h` — `signal()`, `raise()`, `sigprocmask()`
- `unistd.h` — all 30 syscall wrappers

</details>

---

## Kernel Panic

When the kernel hits an unrecoverable error it displays a full crash screen:

```
*** KERNEL PANIC ***

  Page Fault (kernel mode)

  EAX=0x00000000  ECX=0x00000000  EDX=0x00000000
  EBX=0x00000000  ESI=0x00000000  EDI=0x00000000
  EBP=0xC03FFEF0  ESP=0xC03FFE80
  EIP=0xC0101234  ERR=0x00000002  CR2=0xDEADBEEF
  VEC=14

Stack trace:
  #0  0xC010ABCD
  #1  0xC010EF01

System halted. Please reboot.
```

---

## Documentation

| Document | Purpose |
|---|---|
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | Subsystem map, dependency graph |
| [`docs/MEMORY_LAYOUT.md`](docs/MEMORY_LAYOUT.md) | Physical + virtual memory maps |
| [`docs/CONVENTIONS.md`](docs/CONVENTIONS.md) | Coding style |
| [`docs/COMMIT_CONVENTIONS.md`](docs/COMMIT_CONVENTIONS.md) | Git commit format |

---

<p align="center">
<strong>Built from scratch — one instruction at a time</strong><br/>
<sub>© 2026 Noxis OS · <a href="LICENSE">CC BY-NC 4.0</a> · Free to use, not for commercial purposes</sub>
</p>
