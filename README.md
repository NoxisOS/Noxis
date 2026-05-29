# Noxis OS

A 32-bit x86 operating system built from scratch in C11 and NASM assembly. No external libraries, no GRUB, no shortcuts.

## Architecture

- **Language**: C11 (freestanding) + NASM x86 assembly
- **Target**: i686-elf, 32-bit protected mode with paging
- **Kernel type**: Monolithic with layered design
- **Memory**: Higher-half kernel at 0xC0000000, 3 GB user space
- **Filesystem**: FAT12/16 via VFS abstraction
- **Toolchain**: Custom `i686-elf-gcc` cross-compiler + `i686-elf-ld`

## Building

Requires an `i686-elf` cross-compiler. See `.opencode/skills/cross-compiler.md` for build instructions.

```bash
make            # Build kernel ELF
make iso        # Create bootable ISO
make run        # Run in QEMU
make run-debug  # Run with GDB debugging enabled
```

## Documentation

| Document                          | Purpose                         |
|-----------------------------------|---------------------------------|
| `docs/ARCHITECTURE.md`            | Full subsystem design + justifications |
| `docs/MEMORY_LAYOUT.md`           | Physical + virtual memory maps  |
| `docs/CONVENTIONS.md`             | Code conventions — single source of truth |
| `docs/COMMIT_CONVENTIONS.md`      | Git commit format + branching   |
| `.opencode/skills/`               | 10 skill files for subsystems   |
| `.opencode/agents/`               | 5 specialized agents            |

## License

Proprietary — internal project.
