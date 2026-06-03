# x86_64 Port — Progress & Plan

Branch: **`x86_64`**

Porting Noxis from i386 (32-bit protected mode) to x86-64 (long mode).
This is a multi-phase effort; each phase must boot before the next begins.

---

## ✅ Phase 1 — Long-mode bring-up (DONE)

Pure-NASM boot sector, no cross-compiler needed.

- `src/boot64/boot.asm` — real mode → 32-bit protected → long mode
- 4-level paging (PML4 → PDPT → PD), identity-maps first 2 MB with a 2 MB page
- GDT with 32-bit code, data, and 64-bit code (L=1) segments

## ✅ Phase 2 — 64-bit C kernel (DONE)

- `src/boot64/entry.asm` — `_start64`: sets RSP, calls `kmain64()`
- `src/boot64/kmain.c` — minimal C kernel, writes coloured text to VGA
- `src/boot64/kernel.ld` — links the kernel flat at `0x10000`
- Boot sector loads the kernel from disk (INT 13h/42h LBA read) then jumps in

```bash
make boot64     # build/boot64.img (boot sector + 64-bit C kernel)
make run64      # boot it in qemu-system-x86_64
```

Verified: 64-bit C `kmain64()` runs, SysV AMD64 calling convention works,
prints 3 coloured lines to VGA. Toolchain: `x86_64-elf-gcc 15.2.0`,
`binutils 2.45` at `D:/Program Files/x86_64-elf-tools-windows/bin`.

---

## Remaining phases

| Phase | Scope | Key changes |
|---|---|---|
| **3** | GDT/IDT/ISR 64-bit | 16-byte IDT gates, `iretq`, new interrupt frame (`rip/rsp/ss` always pushed), TSS with IST |
| **4** | Memory | PMM ~unchanged; VMM rewritten to 4-level (PML4/PDPT/PD/PT); higher-half at `0xFFFFFFFF80000000` |
| **5** | Heap/slab/arena | Mostly `uint32_t`→`size_t`/`uint64_t`, pointer-size fixes |
| **6** | Syscalls | `sysenter`→`syscall`/`sysret` (MSR LSTAR/STAR/SFMASK); System V AMD64 ABI args (RDI/RSI/RDX/R10/R8/R9); rewrite all noxlib asm stubs |
| **7** | Processes | 64-bit context switch, ELF64 loader, ring-3 via `sysret`/`iretq` |
| **8** | NoxFS | On-disk format: decide which fields grow to 64-bit; update the disk builders |
| **9** | Userland | Recompile nsh + noxlib for x86_64 |

---

## Gotchas specific to this codebase

- **Kernel must use `-mno-red-zone`** — the SysV red zone is unsafe in interrupt handlers.
- **`-mcmodel=kernel`** for the higher-half kernel (code in the top 2 GB).
- **`uint32_t` used as pointers** — grep for `(uint32_t)` casts on addresses; these break.
- **NoxFS inode/superblock** are packed structs with `uint32_t` fields — they stay 32-bit on disk by choice, but the in-memory pointers around them become 64-bit.
- **`page_dir_phys`, `kstack_top`, all addresses in `process_t`** must become `uint64_t`.
- **VGA/serial drivers** barely change (port I/O is identical).
