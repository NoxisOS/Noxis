# Contributing to Noxis OS

First off — thanks for your interest. Noxis is a hobby kernel project and
contributions are welcome as long as they respect the project's philosophy:
**no external libraries, no shortcuts, everything from scratch.**

---

## Before You Start

- Read [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) to understand the
  subsystem layout and dependency rules.
- Read [`docs/CONVENTIONS.md`](docs/CONVENTIONS.md) for coding style.
- Read [`docs/COMMIT_CONVENTIONS.md`](docs/COMMIT_CONVENTIONS.md) for
  commit message format.

---

## What We Accept

| Type | Welcome? |
|---|---|
| Bug fixes | ✅ Always |
| New kernel subsystems (drivers, syscalls, mm) | ✅ If well scoped |
| Userland programs / noxlib additions | ✅ |
| Documentation improvements | ✅ |
| Refactors that break the layering rules | ❌ |
| External library dependencies | ❌ Never |
| GRUB / multiboot support | ❌ Out of scope |

---

## Development Setup

```bash
# 1. Build a cross-compiler (required)
# See .opencode/skills/cross-compiler.md

# 2. Clone and build
git clone https://github.com/gabinschiro/noxis.git
cd noxis
make

# 3. Run in QEMU
make run
```

---

## Workflow

1. **Fork** the repository and create a branch from `main`.
2. **Write your code** following the conventions.
3. **Test** with `make run` and `make run-headless`.
4. **Commit** using the conventional format:
   ```
   feat(mm): add huge page support
   fix(noxfs): prevent double-free on unlink
   docs(readme): update syscall table
   ```
5. **Open a Pull Request** — fill in the PR template completely.

---

## Coding Rules

- C11 freestanding only — no `<stdlib.h>`, `<stdio.h>`, or any hosted header
  in kernel code.
- No upward dependencies — a lower-layer module must never include a
  higher-layer header.
- Every `kmalloc` must have a matching `kfree` path — run `memstat` to verify.
- New syscalls must have a corresponding stub in `noxlib/sys/syscall.asm`
  and a declaration in `noxlib/include/unistd.h`.

---

## License

By contributing, you agree that your contributions will be licensed under
the same [CC BY-NC 4.0](LICENSE) license that covers the project.
