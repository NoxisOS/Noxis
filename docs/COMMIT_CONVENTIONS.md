# Noxis OS — Commit Conventions

## 1. Commit Message Format

```
<type>(<scope>): <subject>

<body>
```

### Types

| Type       | Usage                                                    |
|------------|----------------------------------------------------------|
| `feat`     | New feature (new subsystem, new driver, new syscall)     |
| `fix`      | Bug fix                                                  |
| `refactor` | Code restructuring without behavior change               |
| `perf`     | Performance improvement                                  |
| `style`    | Naming, formatting, comments (no logic change)           |
| `docs`     | Documentation only                                       |
| `test`     | Adding or fixing tests                                   |
| `chore`    | Build system, tooling, scripts                           |
| `hal`      | Hardware Abstraction Layer changes                       |
| `asm`      | Assembly code changes (bootloader, ISR stubs, switch)    |

### Scopes

| Scope      | Directory / subsystem                               |
|------------|-----------------------------------------------------|
| `boot`     | `src/boot/`                                         |
| `gdt`      | `src/hal/gdt.c`                                     |
| `idt`      | `src/hal/idt.c`                                     |
| `pic`      | `src/hal/pic.c`                                     |
| `pit`      | `src/hal/pit.c`                                     |
| `pmm`      | `src/mm/pmm.c`                                      |
| `vmm`      | `src/mm/vmm.c`                                      |
| `heap`     | `src/mm/heap.c`                                     |
| `vga`      | `src/drivers/vga.c`                                 |
| `kbd`      | `src/drivers/keyboard.c`                            |
| `ata`      | `src/drivers/ata.c`                                 |
| `isr`      | `src/kernel/isr.c` + `src/asm/isr_stubs.asm`        |
| `panic`    | `src/kernel/panic.c`                                |
| `vfs`      | `src/vfs/`                                          |
| `proc`     | `src/proc/`                                         |
| `syscall`  | `src/syscall/`                                      |
| `build`    | Makefile, linker script, toolchain                  |
| `docs`     | `docs/`                                             |
| `arch`     | Architecture-wide changes crossing multiple scopes  |

### Subject Rules

- Lowercase, present tense imperative: "add", "fix", "remove" — NOT "added", "fixes"
- Max 72 characters
- No trailing period
- Describe **what** the commit does, not **how**

### Body (optional but recommended)

- Wrap at 72 characters
- Explain **why** the change was made
- Reference hardware specs if relevant

### Examples

```
feat(pmm): add physical frame allocator with bitmap

Implements pmm_alloc_frame() and pmm_free_frame() using a bitmap
tracking 4 GB of physical memory in 4 KB frames. Pre-marks BIOS
regions, kernel image, and bitmap itself as used.
```

```
fix(idt): correct gate type for IRQ handlers

IRQ handlers were using interrupt gates (0x8E) instead of trap gates
(0xEF). Interrupt gates clear IF on entry which prevents nested
interrupts. Changed to trap gates to allow PIC to deliver higher
priority IRQs during handler execution.
Ref: Intel Manual Vol.3 §6.12.1.2
```

```
asm(boot): enter protected mode from real mode

Sets up temporary GDT with 4 GB flat code/data segments.
Enables A20 gate via keyboard controller method 2.
Sets CR0.PE bit and far-jumps to 32-bit code segment.
```

---

## 2. Branching Strategy

```
main
  │
  ├── feat/<name>        Feature branches (new subsystem, new driver)
  ├── fix/<name>         Bug fix branches
  ├── refactor/<name>    Refactoring branches
  └── docs/<name>        Documentation branches
```

### Branch Naming

- All lowercase
- Hyphen-separated words
- Prefix with type: `feat/`, `fix/`, `refactor/`, `docs/`
- Examples: `feat/paging`, `fix/triple-fault`, `refactor/pmm-bitmap`

### Workflow

1. Create branch from `main`: `git checkout -b feat/paging`
2. Make atomic commits following commit conventions
3. Squash WIP commits before merging
4. Merge via fast-forward when possible

### Rules

- `main` must always build and boot successfully
- Never commit directly to `main` — always use branches
- Each commit should be a logical unit (one change, one commit)
- No "WIP", "tmp", "fixup" commits in `main` history
- Before merging, rebase on `main` and squash fixup commits

---

## 3. Pre-Commit Checklist

Before every commit, verify:

- [ ] Code compiles with `-Wall -Wextra -Werror` (zero warnings)
- [ ] Assembly assembles with NASM (zero errors)
- [ ] Linker produces valid ELF
- [ ] QEMU boots successfully
- [ ] No debug printfs left in code
- [ ] No commented-out code (remove it)
- [ ] File header present on every new file
- [ ] Function doc blocks present on all public functions
- [ ] `static` on all file-scope functions and variables
- [ ] `os_status_t` return on all functions that can fail
