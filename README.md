<div align="center">

<img src="docs/logo_noxis.ico" alt="Noxis OS Logo" width="120" />

<br/>

<h1>Noxis OS</h1>
<p><strong>A 64-bit hobby kernel built from scratch</strong></p>

<p>
<a href="../../wiki/Architecture-Overview"><strong>Architecture</strong></a> ·
<a href="../../wiki/Memory-Management"><strong>Memory</strong></a> ·
<a href="../../wiki/Syscall-Reference"><strong>Syscalls</strong></a> ·
<a href="../../wiki"><strong>Wiki</strong></a>
</p>

<div>
<img src="https://img.shields.io/badge/Language-C11%20%2B%20NASM-blue?style=for-the-badge&logo=c" alt="Language"/>
<img src="https://img.shields.io/badge/Architecture-x86__64-orange?style=for-the-badge&logo=intel" alt="Architecture"/>
<img src="https://img.shields.io/badge/Kernel-Monolithic-green?style=for-the-badge" alt="Kernel Type"/>
<img src="https://img.shields.io/badge/License-CC%20BY--NC%204.0-lightgrey?style=for-the-badge" alt="License"/>
</div>

</div>

---

> No external libraries. No GRUB. No shortcuts.
> Every byte — from the boot sector to the shell — written by hand.

---

## ⚡ Quick Start

Requires an `x86_64-elf` cross-compiler and NASM + QEMU. See [Cross-Compiler Setup](../../wiki/Cross-Compiler-Setup).

```bash
make              # build kernel + userland + disk images
make run          # QEMU — VGA window + serial on stdout
make run-headless # QEMU — serial only, no window
```

---

## What is Noxis?

Noxis is a monolithic **64-bit** operating system written entirely in **C11** and **NASM assembly** — no libc, no GRUB, no borrowed code.

A hand-written 512-byte boot sector takes the CPU from real mode through protected mode into **x86-64 long mode**, builds 4-level paging, and jumps to a **higher-half kernel**. The kernel brings up the platform, then launches a Unix-like shell (`nsh`) as a ring-3 user process. From there you get real **preemptive multitasking** with **per-process address spaces**, `fork`/`exec`/`wait`, pipes, signals, and file descriptors.

---

## Architecture

| Item | Value |
|---|---|
| Language | C11 (freestanding) + NASM x86-64 |
| Target | `x86_64-elf`, long mode + 4-level paging |
| Kernel type | Monolithic, layered |
| Kernel base | `0xFFFFFFFF80010000` (higher-half) |
| Physmap | all RAM at `0xFFFF800000000000` |
| User space | private `PML4[0]` per process |
| Syscall ABI | `syscall` / `sysret` (SysV-style) |
| Toolchain | `x86_64-elf-gcc` + `x86_64-elf-ld` + `nasm` |

---

## What's inside

<details open>
<summary><strong>🥾 Boot</strong></summary>

- Hand-written 512-byte boot sector in NASM — no GRUB
- A20 enable, real mode → protected mode → **long mode**
- Builds initial 4-level page tables (identity + higher-half), loads the kernel via INT 13h LBA
- Low→high trampoline into the higher-half kernel; serial boot log on COM1

</details>

<details open>
<summary><strong>🔧 HAL</strong></summary>

- 64-bit **GDT** (kernel/user code+data) + **TSS** (`rsp0` for ring-3 → ring-0)
- 64-bit **IDT** — 16-byte gates, 32 exceptions + 16 IRQs
- 8259A **PIC** remapped to vectors 32–47 (EOI before dispatch)
- **PIT** channel 0 at 1 kHz — uptime + preemption clock
- **x87 / SSE** brought up (CR0/CR4)

</details>

<details open>
<summary><strong>🧠 Memory Management</strong></summary>

- **PMM** — bitmap frame allocator over detected RAM
- **VMM** — 4-level paging (PML4/PDPT/PD/PT), 2 MB pages for the maps
- **Physmap** — all physical RAM mapped at `0xFFFF800000000000`, so the kernel reaches any frame without a low identity map
- **Per-process address spaces** — `PML4[0]` is private per process; physmap (`[256]`) and kernel (`[511]`) are shared
- **Kernel heap** — first-fit `kmalloc`/`kfree` with coalescing, living behind the physmap
- **Slab** + per-process **arena** allocators

</details>

<details open>
<summary><strong>💽 Drivers</strong></summary>

- VGA 80×25 text mode — putchar, scrolling, 16 colours, CP437 (reached via the physmap)
- PS/2 keyboard — scancode → ASCII, ring buffer, **canonical line editing** (echo + backspace)
- 16550 UART serial (COM1) — kernel debug log
- ATA PIO — sector read/write
- Generic block device layer + buffer cache

</details>

<details open>
<summary><strong>📂 Filesystem</strong></summary>

- **VFS** — file abstraction backing the fd layer
- **NoxFS** — inode-based on-disk filesystem (superblock, bitmaps, inode table, directory entries); read **and** write
- **Per-process fd table** — `open` / `close` / `read` / `write` / `lseek` / `dup` / `dup2`, with stdin/stdout/stderr wired to the console
- **Pipes** — anonymous, blocking, refcounted, inherited across `fork`

</details>

<details open>
<summary><strong>⚙️ Processes & Scheduling</strong></summary>

- `fork` — full copy of the user address space; child resumes with `rax = 0`
- `exec` — load an ELF64 into a fresh address space, with `argv` on the user stack
- `exit` / `waitpid` — zombie collection + exit status; `getpid`
- **Preemptive round-robin scheduler** — PIT-driven, switches CR3 + `TSS.rsp0` per process
- Ring-3 user mode in **isolated address spaces**; ring-0 kernel threads run alongside
- `ps` lists live processes via a `procinfo` syscall

</details>

<details open>
<summary><strong>📡 Signals</strong></summary>

- `signal(sig, handler)` installs a user handler (inherited across `fork`)
- `kill(pid, sig)` marks a signal pending; delivered at the target's next syscall return
- User handlers run on the user stack (original RIP pushed so the handler `ret`s back), signo in `rdi`
- Default action terminates the process for `SIGINT` / `SIGTERM` / `SIGKILL`

</details>

<details open>
<summary><strong>🔌 Syscall Interface</strong></summary>

`syscall` / `sysret` fast path (SysV-style: `rax` = number, args in `rdi`, `rsi`, `rdx`). The entry stub captures the full user register frame so `fork` can clone it.

| # | Name | # | Name | # | Name |
|---|---|---|---|---|---|
| 0 | `exit` | 6 | `waitpid` | 12 | `dup2` |
| 1 | `write` | 7 | `open` | 13 | `pipe` |
| 2 | `read` | 8 | `close` | 14 | `kill` |
| 3 | `fork` | 9 | `lseek` | 15 | `signal` |
| 4 | `exec` | 10 | `readdir` | 16 | `procinfo` |
| 5 | `getpid` | 11 | `dup` | | |

</details>

<details open>
<summary><strong>🐚 nsh — The Shell</strong></summary>

There is no in-kernel shell. `nsh` is a ring-3 user process launched by the kernel as the init program. It tokenizes a line and `fork`+`exec`s external programs.

```
nsh$ ls.elf
nsh.elf
hello.elf
echo.elf
cat.elf
ls.elf
ps.elf
motd.txt

nsh$ ls.elf | cat.elf
nsh$ echo.elf written to a file > out.txt
nsh$ cat.elf out.txt
written to a file
```

- Pipelines: `cmd1 | cmd2`
- Redirections: `>`, `>>`, `<`
- Built-ins: `exit`, `help`
- External programs on the NoxFS disk: `ls`, `echo`, `cat`, `ps`, `sigtest`, `hello`

</details>

<details open>
<summary><strong>📦 noxlib — User-space C Runtime</strong></summary>

A minimal runtime linked into every userland ELF (`src/noxlib/`):

- `crt0.asm` — `_start` reads `argc`/`argv` off the stack, calls `main`, then `exit`s with its return value
- Inline syscall wrappers: `write`, `read`, `open`, `close`, `lseek`, `dup`/`dup2`, `pipe`, `fork`, `execv`, `waitpid`, `getpid`, `kill`, `signal`, `readdir`, `procinfo`
- Helpers: `puts`, `puti`, `strlen`, `strcmp`

</details>

---

## Project layout

```
src/
├── boot/        boot sector + low→high kernel entry
├── kernel/
│   ├── core/    kernel_main bring-up
│   ├── hal/     gdt, idt, pic, fpu
│   ├── isr/     interrupt stubs + dispatcher
│   └── syscall/ syscall/sysret entry + dispatcher
├── mm/          pmm, vmm (physmap + address spaces), heap, slab, arena
├── proc/        process, scheduler, fork/exec/wait, fd table, pipes, signals, ELF loader
├── drivers/     vga, kbd, serial, pit, ata, block, keymap
├── fs/          noxfs (on-disk) + vfs + buffer cache
├── noxlib/      user-space C runtime (crt0 + syscall wrappers)
└── userland/    nsh + coreutils (ls, echo, cat, ps, …)
```

---

## Status

This is the **x86-64 port**. The foundation — long mode, higher-half kernel, per-process address spaces, preemptive scheduling, `fork`/`exec`/`wait`, fd table, pipes, signals, and a fork/exec shell — is in place and boots to an interactive prompt.

Not yet ported / planned: copy-on-write fork, demand paging, a mounted synthetic FS (`/proc`, `/dev` as paths), full `termios` (raw mode, `Ctrl-C` → `SIGINT`), `cd`/cwd + path resolution, and reclaiming a dead process's memory.

---

## Documentation

The full documentation lives in the [**Wiki**](../../wiki):

| Page | Purpose |
|---|---|
| [Architecture Overview](../../wiki/Architecture-Overview) | Subsystem map |
| [Boot Process](../../wiki/Boot-Process) | Real → long mode, higher half |
| [Memory Management](../../wiki/Memory-Management) | PMM, VMM, physmap, heap |
| [Processes and Scheduling](../../wiki/Processes-and-Scheduling) | fork/exec/wait, scheduler |
| [Syscall Reference](../../wiki/Syscall-Reference) | The full syscall table |
| [Filesystem](../../wiki/Filesystem) | NoxFS, VFS, fds, pipes |
| [nsh — The Shell](../../wiki/nsh-—-The-Shell) | Shell usage |
| [Building from Source](../../wiki/Building-from-Source) | Toolchain + build |

---

<p align="center">
<strong>Built from scratch — one instruction at a time</strong><br/>
<sub>© 2026 Noxis OS · <a href="LICENSE">CC BY-NC 4.0</a> · Free to use, not for commercial purposes</sub>
</p>
