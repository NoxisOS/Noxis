# Noxis OS — Architecture Design

## 1. Design Philosophy

Noxis is built from first principles following these axioms:

1. **No external code.** Every byte is ours. No GRUB, no libraries, no vendor code.
2. **Layered isolation.** Each layer talks only to the layer directly below it.
3. **File descriptor universe.** Everything at the VFS layer is accessed through the same fd interface: files, pipes, devices, processes.
4. **Defensive by default.** Every pointer checked, every return code handled, every hardware access assumes failure.
5. **Single responsibility.** One `.c` file = one subsystem = one responsibility.

---

## 2. Subsystem Inventory

| #  | Subsystem            | Layer | Responsibility                                                  |
|----|----------------------|-------|-----------------------------------------------------------------|
| 1  | Bootloader           | 0     | Enter protected mode, load kernel, set up initial GDT, jump     |
| 2  | HAL — GDT            | 1     | Global Descriptor Table: segments, TSS, privilege levels        |
| 3  | HAL — IDT            | 1     | Interrupt Descriptor Table: register handlers, trap gates       |
| 4  | HAL — PIC            | 1     | Remap PIC, mask/unmask IRQs, send EOI                           |
| 5  | HAL — Ports          | 1     | Raw port I/O primitives (inb, outb, inw, outw, inl, outl)      |
| 6  | HAL — PIT            | 1     | Configure PIT channel 0, set frequency                          |
| 7  | Kernel — Early Init  | 2     | BSS zeroing, call HAL init, remap PICs, jump to main            |
| 8  | Kernel — Panic       | 2     | Fatal error handler, dump registers, halt                       |
| 9  | Kernel — ISR         | 2     | C-side ISR dispatcher, fan-out to registered handlers           |
| 10 | MM — PMM             | 3     | Physical Memory Manager: track free frames, alloc/free          |
| 11 | MM — VMM             | 3     | Virtual Memory Manager: page directory/tables, map/unmap        |
| 12 | MM — Heap            | 3     | Kernel heap: kmalloc/kfree on top of VMM                        |
| 13 | MM — Paging Ops      | 3     | Page table walking, invalidation, CR3 manipulation              |
| 14 | Drivers — VGA        | 4     | 80×25 text mode: putchar, scrolling, colors                     |
| 15 | Drivers — Keyboard   | 4     | PS/2 keyboard: scancode→ASCII, buffered input                   |
| 16 | Drivers — PIT Driver | 4     | PIT IRQ0 handler, tick counting, sleep/uptime                   |
| 17 | Drivers — ATA        | 4     | ATA PIO read/write sectors                                      |
| 18 | VFS                  | 5     | Virtual file descriptor table, mount points, generic ops        |
| 19 | FS                   | 5     | Filesystem implementation (FAT12/16 initially)                  |
| 20 | Proc — Manager       | 6     | Process lifecycle: create, fork, exec, exit, wait               |
| 21 | Proc — Scheduler     | 6     | Round-robin with priority, context switch                       |
| 22 | Proc — Switch        | 6     | ASM context save/restore                                        |
| 23 | Syscall — Table      | 7     | Syscall dispatch table, interrupt 0x80 entry                    |
| 24 | Syscall — Handlers   | 7     | sys_read, sys_write, sys_open, sys_fork, etc.                   |

---

## 3. Dependency Graph

```
                          ┌──────────────────┐
                          │     Userland      │
                          │   (ring 3)        │
                          └────────┬─────────┘
                                   │ int 0x80
                          ┌────────┴─────────┐
                          │  Syscall Table   │
                          │  syscall/        │
                          └────────┬─────────┘
                                   │
              ┌────────────────────┼────────────────────┐
              │                    │                    │
     ┌────────┴────────┐  ┌───────┴───────┐  ┌────────┴────────┐
     │  Process Mgr    │  │     VFS       │  │  Syscall Fns    │
     │  proc/          │  │     vfs/      │  │  syscall/       │
     └────────┬────────┘  └───────┬───────┘  └────────┬────────┘
              │                   │                   │
     ┌────────┴────────┐          │                   │
     │   Scheduler     │          │                   │
     │   + switch.asm  │          │                   │
     └────────┬────────┘          │                   │
              │                   │                   │
              └───────────┬───────┴───────────────────┘
                          │
                 ┌────────┴────────┐
                 │  Memory Manager │
                 │  mm/pmm + vmm   │
                 │  + heap         │
                 └────────┬────────┘
                          │
                    ┌─────┴─────┐
                    │  Drivers  │
                    │  drivers/ │
                    └─────┬─────┘
                          │
                    ┌─────┴─────┐
                    │    HAL    │
                    │    hal/   │
                    └─────┬─────┘
                          │
                    ┌─────┴─────┐
                    │ Bootloader│
                    │ + early   │
                    └─────┬─────┘
                          │
                    ┌─────┴─────┐
                    │  Hardware │
                    └───────────┘
```

**Invariant: No arrow ever points upward.** No circular dependencies exist.

---

## 4. Interface Contracts

Every layer exposes a `.h` header that defines its public interface. No implementation details leak through headers. Private functions are always `static`.

### HAL → Above
- `gdt_init()` — load GDT, set segments
- `idt_init()`, `idt_set_gate(n, handler, flags)` — set up IDT
- `pic_remap()`, `pic_send_eoi(irq)`, `pic_mask(irq)`, `pic_unmask(irq)`
- `port_byte_in(port)`, `port_byte_out(port, val)`, etc.
- `pit_set_frequency(hz)` — configure timer

### MM → Above
- `pmm_alloc_frame()` → physical address
- `pmm_free_frame(phys_addr)`
- `vmm_map_page(virt, phys, flags)` — map 4 KB page
- `vmm_unmap_page(virt)`
- `vmm_alloc_page(virt, flags)` — alloc + map
- `kmalloc(size)` / `kfree(ptr)` — kernel heap

### Drivers → Above
- `vga_put_char(c)`, `vga_write(str)`, `vga_clear()`
- `kbd_read()` — blocking read, returns scancode/char
- `pit_sleep_ms(ms)`, `pit_uptime_ms()`
- `ata_read_sector(lba, buf)`, `ata_write_sector(lba, buf)`

### VFS → Above
- `vfs_open(path, flags)` → fd
- `vfs_read(fd, buf, count)` → bytes read
- `vfs_write(fd, buf, count)` → bytes written
- `vfs_close(fd)`
- `vfs_mount(device, path, fs_type)`

### Proc → Above
- `proc_spawn(entry)` → pid
- `proc_exit(code)`
- `scheduler_yield()` — trigger context switch
- `scheduler_tick()` — called from PIT ISR

### Syscall → Above (userland)
- `int 0x80` interface with call number in EAX

---

## 5. Boot Sequence

```
Power-on → BIOS → MBR (0x7C00) → Stage2 → Protected Mode → Kernel

1. BIOS loads MBR at 0x7C00
2. MBR relocates to 0x0600, loads Stage2 from disk
3. Stage2 enables A20, loads kernel to 0x100000
4. Stage2 sets up temporary GDT, switches to protected mode
5. Far jump to kernel entry at 0x100000
6. Kernel sets up proper GDT, remaps PIC, initializes VGA
7. Kernel sets up paging, enables virtual memory
8. Kernel initializes PMM using multiboot memory map
9. Kernel initializes heap
10. Kernel initializes all drivers
11. Kernel initializes VFS, mounts root
12. Kernel enters scheduler, launches init process
```

---

## 6. Justifications

**Why monolithic kernel?**
A 32-bit OS targeting a single CPU with no MMU protection ring transitions between microkernel servers would add context-switch overhead with no benefit. Monolithic preserves simplicity while keeping the layered design for code organization.

**Why higher-half kernel?**
Mapping the kernel at 0xC0000000 (3 GB) leaves a full 3 GB contiguous range for user processes. It also protects the kernel from null-pointer dereferences in user code (0x00000000–0xBFFFFFFF are entirely user space).

**Why identity-map initially then higher-half?**
The CPU starts in physical-address mode. We identity-map the first 4 MB during boot, initialize paging structures in that identity-mapped window, then jump to the higher-half mapping. This is the standard "trampoline" approach used by Linux and others.

**Why FAT12/16 as first filesystem?**
FAT is the simplest widely-documented filesystem. It requires minimal code (< 500 lines), has no journaling, and is well supported by external tools for creating disk images.

**Why no GRUB?**
GRUB does too much: it sets up protected mode, parses ELF, and provides a memory map. We want to understand and own every one of those steps. Our bootloader will be ~800 lines of NASM — fully understandable and debuggable.

**Why 32-bit and not 64-bit?**
32-bit protected mode with paging is the foundational stepping stone. The paging model (two-level: PD → PT) is simpler than 64-bit long mode (four-level). All concepts transfer directly. We can add 64-bit support later as a natural evolution.

