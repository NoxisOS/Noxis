# Noxis OS

A 32-bit x86 operating system built from scratch in C11 and NASM assembly. No external libraries, no GRUB, no shortcuts.

## What's implemented

### Boot
- Custom two-stage bootloader (MBR + Stage2) in NASM — no GRUB
- A20 line enable, real mode → protected mode transition
- Kernel loaded at 1 MB, higher-half mapping to 0xC0000000
- Serial boot log output on COM1 for early debugging

### HAL (Hardware Abstraction Layer)
- GDT with kernel/user segments + TSS for privilege switching
- IDT with 256 gates — exceptions (0–31) + IRQs (32–47) + syscall (0x80)
- PIC remapped to IRQ 32–47, mask/unmask/EOI
- PIT (channel 0) at configurable frequency — tick counter, `pit_sleep_ms`, `pit_uptime_ms`
- Raw port I/O (`inb`/`outb`/`inw`/`outw`/`inl`/`outl`)
- x87 FPU with lazy context switching (`#NM`/`#TS` fault-driven save/restore)

### Memory Management
- Physical Memory Manager (PMM) — bitmap allocator over detected RAM
- Virtual Memory Manager (VMM) — two-level page directory/table, higher-half kernel
- Demand paging with page fault handler (`#PF`) — pages allocated on first access
- Kernel heap (`kmalloc`/`kfree`) on top of VMM
- User-space heap via `brk`/`sbrk` syscalls with demand paging
- Per-process address space with isolated page directories
- User stack allocated and mapped on `exec`

### Drivers
- VGA 80×25 text mode — putchar, scrolling, colors
- PS/2 keyboard — scancode→ASCII, buffered input
- 16550 UART serial driver (COM1) — kernel debug output
- ATA PIO — sector read/write
- Generic block device layer (registration, dispatch)
- TTY subsystem — canonical/raw mode, signal delivery on Ctrl+C/Ctrl+Z

### Filesystem
- VFS layer — unified fd interface, mount points, generic ops (open/read/write/close/lseek/stat)
- RamFS — in-memory filesystem for early boot
- NoxFS v2 — inode-based filesystem with subdirectories, `mkdir`, `readdir`, `stat`, `lseek`
- Block buffer cache with LRU eviction
- Anonymous pipes with blocking I/O and fd inheritance across `fork`

### Processes
- Process lifecycle — `fork`, `exec`, `exit`, `wait`
- ELF32 loader — parse and load flat ELF executables
- Round-robin scheduler with preemption via PIT IRQ0
- ASM context switch (`kthread_switch.asm`)
- Ring-3 user mode — `iret` into user space with CPL=3
- Signal handling — `kill`, `sigaction`, delivery on syscall return (SIGKILL, SIGUSR1, SIGCHLD…)

### Syscall Interface
- `int 0x80` dispatch table
- `sysenter`/`sysexit` fast path (MSR-based)
- Implemented syscalls: `read`, `write`, `open`, `close`, `fork`, `exec`, `exit`, `wait`, `getpid`, `getppid`, `getuid`, `time`, `dup2`, `sleep`, `brk`, `sbrk`, `pipe`, `kill`, `sigaction`

### Shell
- Interactive kernel shell over VGA + keyboard
- Commands: `help`, `ls`, `cd`, `cat`, `mkdir`, `clear`, `sleep`, `uptime`, `halt`, `exec`, `blkstat`

### Userland programs (NASM)
- `init` — PID 1, launches shell
- `hello`, `echo`, `prompt`, `write`, `fork`, `pipe`, `signal`, `ttytest`
- Test programs: `brktest`, `pftest`, `segv`, `fputest`, `systest`, `fread`

---

## Architecture

- **Language**: C11 (freestanding) + NASM x86 assembly
- **Target**: i686-elf, 32-bit protected mode with paging
- **Kernel type**: Monolithic with strict layered design (no upward dependencies)
- **Memory layout**: Higher-half kernel at 0xC0000000, 3 GB user space
- **Toolchain**: Custom `i686-elf-gcc` cross-compiler + `i686-elf-ld`

## Building

Requires an `i686-elf` cross-compiler. See [`.opencode/skills/cross-compiler.md`](.opencode/skills/cross-compiler.md) for build instructions.

```bash
make            # Build kernel ELF
make iso        # Create bootable ISO
make run        # Run in QEMU (serial console)
make run-debug  # Run with GDB stub enabled
```

## Documentation

| Document | Purpose |
|---|---|
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | Subsystem inventory, dependency graph, design justifications |
| [`docs/MEMORY_LAYOUT.md`](docs/MEMORY_LAYOUT.md) | Physical + virtual memory maps |
| [`docs/CONVENTIONS.md`](docs/CONVENTIONS.md) | Code conventions — single source of truth |
| [`docs/COMMIT_CONVENTIONS.md`](docs/COMMIT_CONVENTIONS.md) | Git commit format + branching |
| [`.opencode/skills/`](.opencode/skills/) | Per-subsystem implementation skills |
| [`.opencode/agents/`](.opencode/agents/) | Specialized agents (architect, asm, memory, debugger, driver) |

## License

Proprietary — internal project.
