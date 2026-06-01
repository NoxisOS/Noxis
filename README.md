# Noxis OS

A 32-bit x86 operating system built from scratch in C11 and NASM assembly.  
No external libraries, no GRUB, no shortcuts.

---

## What's implemented

### Boot
- Custom two-stage bootloader (MBR + Stage 2) in NASM — no GRUB
- A20 line enable, real mode → protected mode transition
- Kernel loaded at 1 MB, higher-half mapped to `0xC0000000`
- Serial boot log on COM1 (`-serial stdio`) for headless debugging

### HAL
- GDT with kernel/user segments + TSS for ring-0 stack switching
- IDT — 256 gates: exceptions (0–31), IRQs (32–47), syscall int `0x80` (128)
- 8259A PIC remapped to IRQ 32–47, per-IRQ mask/unmask/EOI
- PIT channel 0 at 1 kHz — `pit_uptime_ms()`, preemption clock
- x87 FPU with lazy context switching (`#NM` fault-driven save/restore)
- Raw port I/O (`inb`/`outb`/`inw`/`outw`/`inl`/`outl`)

### Memory Management
- **PMM** — bitmap allocator over all detected RAM (128 MB by default)
- **VMM** — two-level page directory/table, recursive mapping, `vmm_fork_pd`
- **Demand paging** — `#PF` handler grows user stack and heap on first access
- **Kernel heap** — first-fit `kmalloc`/`kfree` with coalescing, at `0xC0400000`
- **User heap** — `brk`/`sbrk` syscalls, pages faulted in on demand
- Per-process isolated address spaces
- **Copy-on-write fork** — `vmm_fork_pd` shares pages read-only between
  parent and child (PAGE_COW bit); the first write in either process
  faults and triggers a private copy. Backed by per-frame reference
  counts in the PMM, so a shared frame is freed only when its last owner
  exits. Fork is near-instant instead of duplicating the whole address space.
- `vmm_create_pd` / `vmm_destroy_pd` for exec/fork address spaces

#### Lifetime-oriented allocation *(the interesting part)*

Noxis deliberately avoids a single anonymous kernel heap where every
object type gets mixed together. Instead, allocation is split by **type**
and by **lifetime** — an approach mainstream kernels (Linux/BSD/Windows)
don't take at this granularity.

**Typed slab caches** (`mm/slab.c`) — one pool per kernel object type:

| Cache | Object | Alloc / free |
|---|---|---|
| `g_process_slab` | `process_t` | **O(1)** intrusive free-list |
| `g_pipe_slab` | `pipe_t` | **O(1)** intrusive free-list |

- No per-object header — free objects link through themselves
- Always returns zeroed memory
- Frees poison the slot with `0xDEADC0DE` → use-after-free is obvious
- **High-water mark** per cache (`n_alloc_peak`) tracks worst-case pressure
- **Adaptive reclaim** — `slab_reap()` returns fully-idle backing blocks to
  the kernel heap; `memstat reap` triggers it on demand. Hobby kernels
  almost never implement shrink-on-pressure.
- Leaks become *visible*: the `memstat` shell command shows live/free/peak
  counts per cache, so a missing free shows up as a rising `live=` count

**Tagged allocations** (`kmalloc_tagged`) — every kernel allocation is
attributed to a subsystem (`slab`, `arena`, `vfs`, `fs`, `pipe`, …). The
heap keeps per-tag live byte + alloc counts, and `memstat` prints a
per-module breakdown — instant leak triage, finer-grained than FreeBSD's
malloc zones.

**Per-process arenas** (`mm/arena.c`) — region allocator tied to a lifetime:

- All kernel allocations whose lifetime equals a process (e.g. the
  `argv` copy in `execve`) come from that process's `proc->arena`
- Allocation is a pointer bump — **O(1)**, zero fragmentation, no headers
- `arena_destroy()` frees everything in one walk — **O(blocks)**, ≈O(1)
  for a typical process (1–4 blocks)
- This moved the old 512-byte `execve` argv buffer off the 4 KB kernel
  stack and made it scale to arbitrarily many arguments

**Fully closed process lifecycle** — `proc_destroy()` reclaims *everything*:

| Resource | Allocated by | Freed by |
|---|---|---|
| `process_t` | `slab_alloc` | `proc_destroy` → `slab_free` |
| kernel stack (8 KB) | `proc_spawn` (PMM) | `proc_destroy` → `vmm_unmap_page_in` |
| per-process arena | `arena_create` | `proc_destroy` → `arena_destroy` |
| user page directory | `vmm_fork_pd` | `scheduler_exit` → `vmm_destroy_pd` |

Verified leak-free with `memstat` before/after `fork`+`exec`+`waitpid`
cycles: `process_t live=` returns to baseline and the heap stops shrinking.

### Drivers
- VGA 80×25 text mode — putchar, scrolling, 16 colors
- PS/2 keyboard — scancode → ASCII, buffered input
- 16550 UART serial (COM1) — polling TX, kernel debug output
- ATA PIO — sector read/write for the disk filesystem
- Generic block device layer (registration + dispatch)
- **TTY** — canonical/raw mode, echo, line editing, Ctrl+C/Ctrl+D
  - Ctrl+C sends SIGINT to the **currently running foreground process**
  - Falls back to the shell if no child is running

### Filesystem
- **VFS** — unified fd interface, generic `open`/`read`/`write`/`close`/`lseek`/`stat`
- **RamFS** — in-memory FS for early boot assets
- **NoxFS v2** — inode-based, on-disk FS with `mkdir`, `readdir`, `stat`
- Block buffer cache
- **Pipes** — anonymous, bidirectional, blocking I/O; refcounted; inherited by `fork`

### Processes & Scheduling
- `fork` — full address-space copy via `vmm_fork_pd`
- `exec` / `execve` — load flat ELF32 into a fresh address space
- `exit` / `waitpid` — zombie collection, exit code, SIGCHLD
- **Round-robin preemptive scheduler** — PIT IRQ0, configurable quantum
- Kernel thread context switch in pure ASM (`kthread_switch.asm`)
- Ring-3 user mode — `iret` into CPL=3 with isolated address space
- Fork/exec with proper argv passing via `_build_argv_frame`

### Signals *(new)*
| Feature | Status |
|---|---|
| `SIGINT` (Ctrl+C) | ✅ kills foreground child, shell survives |
| `SIGTERM`, `SIGKILL`, `SIGSEGV`, `SIGILL`, `SIGFPE`, `SIGBUS`, `SIGABRT` | ✅ default action (terminate) |
| `SIGCHLD` | ✅ sent to parent on child exit |
| Custom handlers via `signal()` / `sigaction()` | ✅ full save/restore via `sig_ucontext_t` |
| `sigreturn` trampoline (`__sig_restorer`) | ✅ restores all registers + EFLAGS |
| `sigprocmask` — block/unblock signal sets | ✅ |
| Signal delivery interrupts `sleep()` early | ✅ |
| `raise()` | ✅ |
| Process groups / `SIGTSTP` / job control | ❌ not yet |

### Syscall Interface
- **`int 0x80`** dispatch table (ring-3 DPL gate)
- **`sysenter`/`sysexit`** fast path (MSR 0x174–0x176)
- 28 syscalls implemented:

| # | Name | # | Name |
|---|---|---|---|
| 0 | `exit` | 14 | `mkdir` |
| 1 | `write` | 15 | `chdir` |
| 2 | `read` | 16 | `getdents` |
| 3 | `open` | 17 | `stat` |
| 4 | `close` | 18 | `lseek` |
| 5 | `fork` | 19 | `execve` |
| 6 | `waitpid` | 20 | `brk` |
| 7 | `creat` | 21 | `getppid` |
| 8 | `pipe` | 22 | `getuid` |
| 9 | `dup` | 23 | `time` |
| 10 | `sigaction` | 24 | `dup2` |
| 11 | `kill` | 25 | `sleep` |
| 12 | `getpid` | 26 | `sigreturn` |
| 13 | `ioctl` | 27 | `sigprocmask` |

### noxlib (user-space C runtime)
- `crt0.asm` — `_start` → `main()` → `exit()`
- `stdio.h` — `printf`, `snprintf`, `vprintf`, `puts`, `putchar`, `getchar`, `fgets`
- `stdlib.h` — `malloc`/`free`, `exit`, `atoi`, `strtol`, `strtoul`, `abs`
- `string.h` — `strlen`, `strcpy`, `strncpy`, `strcmp`, `strncmp`, `strncat`, `memcpy`, `memset`
- `signal.h` — `signal()`, `raise()`, `sigprocmask()`, `sigset_t` helpers
- `unistd.h` — all 28 syscall wrappers

### Shell — **nsh** (C, compiled against noxlib)
- Pipelines: `cmd1 | cmd2 | cmd3`
- Redirections: `>`, `>>`, `<`
- Background jobs: `cmd &`
- `$?` expansion
- Builtins: `cd`, `pwd`, `ls`, `echo`, `clear`, `exit`, `help`, `memstat`
- External programs via `fork` + `execv`
- `.elf` suffix auto-appended if not found
- **Ctrl+C** kills running child, shell continues
- **Ctrl+D** exits nsh cleanly

### Userland programs
| Program | Description |
|---|---|
| `init.elf` | PID 1 fallback shell (ASM) |
| `hello.elf` | Hello world |
| `loop.elf` | Infinite loop — Ctrl+C test target |
| `fork.elf` | fork/waitpid smoke test |
| `pipe.elf` | Pipe I/O smoke test |
| `signal.elf` | Signal delivery smoke test |
| `pftest.elf` | Page fault / demand paging test |
| `segv.elf` | Intentional SIGSEGV |
| `brktest.elf` | `brk` / user heap test |
| `fputest.elf` | x87 FPU lazy switching test |
| `systest.elf` | Syscall regression suite |
| `ctest.elf` | C runtime / noxlib test |

---

## Architecture

| Item | Value |
|---|---|
| Language | C11 (freestanding) + NASM x86 |
| Target | i686-elf, 32-bit protected mode + paging |
| Kernel type | Monolithic, strict layered (no upward deps) |
| Kernel base | `0xC0100000` (higher-half) |
| User space | `0x00400000` – `0xBFFFFFFF` (3 GB) |
| Kernel stack | 8 KB per process (2 pages at `0xD0000000+`) |
| Toolchain | `i686-elf-gcc` cross-compiler + `i686-elf-ld` |

## Building

Requires an `i686-elf` cross-compiler.  
See [`.opencode/skills/cross-compiler.md`](.opencode/skills/cross-compiler.md).

```bash
make clean && make   # full rebuild (always do this after header changes)
make run             # QEMU: VGA window + serial on stdout
make run-headless    # QEMU: serial only, no window
make run-debug       # QEMU + GDB stub on :1234
```

## Documentation

| Document | Purpose |
|---|---|
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | Subsystem map, dependency graph |
| [`docs/MEMORY_LAYOUT.md`](docs/MEMORY_LAYOUT.md) | Physical + virtual memory maps |
| [`docs/CONVENTIONS.md`](docs/CONVENTIONS.md) | Coding conventions |
| [`docs/COMMIT_CONVENTIONS.md`](docs/COMMIT_CONVENTIONS.md) | Git commit format |

## License

Proprietary — internal project.
