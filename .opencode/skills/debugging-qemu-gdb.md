# Skill: Debugging with QEMU + GDB

## Purpose
This skill covers debugging the Noxis OS kernel using QEMU as the emulator and GDB as the debugger. This is the primary development loop: write code, compile, run in QEMU with GDB, set breakpoints, inspect state.

## Key Concepts

### QEMU Debug Flags

```bash
# Basic debug setup — QEMU waits for GDB to connect
qemu-system-i386 \
    -s -S                          \  # -s: gdbserver on :1234, -S: freeze CPU at start
    -no-reboot -no-shutdown        \  # Don't reboot on triple fault, don't exit on shutdown
    -d cpu_reset,int,guest_errors  \  # Log: CPU resets, interrupt delivery, guest errors
    -D qemu.log                    \  # Write log to file
    -cdrom noxis.iso                   # Our OS image
```

Key QEMU flags:
| Flag               | Purpose                                                        |
|--------------------|----------------------------------------------------------------|
| `-s`               | Start GDB server on `localhost:1234`                           |
| `-S`               | Freeze CPU at startup (wait for GDB `continue`)                |
| `-no-reboot`       | Exit on triple fault instead of rebooting                      |
| `-no-shutdown`     | Don't exit QEMU on guest shutdown (allows post-mortem)         |
| `-d cpu_reset`     | Log every CPU reset (including triple faults)                  |
| `-d int`           | Log every interrupt delivery with vector and error code        |
| `-d guest_errors`  | Log invalid guest behavior (e.g., port access, page faults)    |
| `-d in_asm`        | Log every executed instruction (ENORMOUS output — use sparingly)|
| `-D qemu.log`      | Write log to file instead of stderr                            |
| `-m 128M`          | Set RAM size to 128 MB                                         |
| `-monitor stdio`   | Multiplex QEMU monitor with serial on stdio                    |

### GDB Setup

Create a `.gdbinit` file in the project root:

```
# Connect to QEMU
target remote localhost:1234

# Symbol file for kernel (loaded at virtual address 0xC0100000)
symbol-file build/kernel.elf

# Break at kernel entry
# hbreak is hardware breakpoint — works before paging is set up
hbreak _start
continue
```

**Important on Windows:** GDB may not load `.gdbinit` from the current directory by default. Use `gdb -x .gdbinit` to force it, or set `set auto-load safe-path /` in `~/.gdbinit`.

### GDB Commands Cheat Sheet

```
# Breakpoints
b _start                      # Break at function
b kernel/panic.c:42           # Break at file:line
b *0xC0100000                 # Break at address
hbreak *0x100000              # Hardware breakpoint (for early boot, pre-paging)
info breakpoints              # List breakpoints
delete 1                      # Delete breakpoint 1

# Execution
c                             # Continue
si                            # Step one instruction (into calls)
ni                            # Step one instruction (over calls)
finish                        # Run until current function returns

# Registers
info registers                # Dump all registers
print/x $eax                  # Print register in hex
print $cr2                    # Print control register
set $eax = 0                  # Modify register value

# Memory
x/10wx 0xC0100000             # Examine 10 words in hex at address
x/s 0xC0100000                # Examine as null-terminated string
x/i 0xC0100000                # Examine as instruction (disassemble)
x/20bx $esp                   # Examine 20 bytes at stack pointer

# Stack
bt                            # Backtrace (needs frame pointers)
info frame                    # Current frame info
frame 1                       # Switch to frame 1

# QEMU integration
monitor info registers        # Send command to QEMU monitor
monitor info mem              # Show virtual memory mappings
monitor info tlb              # Show TLB entries
monitor info gdt              # Show GDT
monitor info idt              # Show IDT
monitor xp /10wx 0x100000     # Examine PHYSICAL memory in QEMU
```

### QEMU Monitor Commands

Access via `Ctrl+Alt+2` in QEMU window, or via GDB `monitor <cmd>`:

```
info registers         # Dump CPU registers (more detail than GDB)
info mem               # Virtual memory map (page tables)
info tlb               # TLB contents
info gdt               # GDT entries
info idt               # IDT entries
info pic               # PIC state (IRQ masks, pending)
info pci               # PCI device tree
xp /fmt addr           # Examine PHYSICAL memory
xp /10wx 0xB8000       # View VGA buffer physically
cpu index              # Show CPU state
log in_asm             # Enable instruction-level logging
log exec               # Enable execution tracing
```

### Debugging the Bootloader

The bootloader runs in real mode before GDB connects. To debug it:

```bash
# Start QEMU without -S so the bootloader executes
qemu-system-i386 -s -no-reboot -cdrom noxis.iso &
# Quickly connect GDB
gdb -x .gdbinit
```

Or use `hbreak` at the protected-mode entry point.

### Debugging with Symbols

Our kernel ELF has debug symbols. GDB will resolve addresses to function names and line numbers. For NASM files, use `-g` (debug info) with NASM:

```bash
nasm -f elf32 -g -F dwarf isr_stubs.asm -o isr_stubs.o
```

Note: NASM debug info is limited. You'll get label names but not source line mapping. For detailed ASM debugging, use `layout asm` in GDB.

## Common Debugging Scenarios

### Triple Fault (CPU resets)

A triple fault occurs when an exception occurs while handling a double fault. Common causes:
1. GDT/IDT not loaded or malformed → second fault while trying to deliver the first
2. Stack overflow → page fault while trying to push exception frame
3. Page fault in page fault handler → recursive fault

**Debugging procedure:**
1. Look at `qemu.log` for `cpu_reset` events — QEMU logs the reason
2. Enable `-d int` to see which exception fires first
3. Check that the IDT is loaded before enabling interrupts
4. Check that the page fault handler's page is mapped and present

### Page Fault (Vector 14)

When a page fault occurs:
1. Read CR2 — this is the address that caused the fault
2. Read the error code pushed on the stack:
   - Bit 0: 0 = not-present, 1 = protection
   - Bit 1: 0 = read, 1 = write
   - Bit 2: 0 = supervisor, 1 = user
   - Bit 3: 1 = reserved bit violation
   - Bit 4: 1 = instruction fetch

**In GDB:**
```
monitor info registers   # shows CR2 among others
x/1wx $esp               # error code is at top of stack in ISR
monitor info mem         # check if the page is mapped
monitor info tlb         # check TLB for stale entries
```

### Kernel Panic

When the kernel calls `panic()`, it dumps registers and halts. To inspect:
1. Read the register dump from VGA output
2. In GDB, `bt` to see the call stack (if frame pointers are set up)
3. Check the panic message for the file and line number
4. Set a breakpoint on `kernel_panic` to catch it before the dump

### "No symbol table" or "??" in GDB

- Make sure the ELF was compiled with `-g`
- Make sure `symbol-file` points to the correct ELF
- For early boot (before paging), symbols may be at wrong addresses because the kernel runs at physical address 0x100000 but symbols are at 0xC0100000. Use `add-symbol-file kernel.elf -o -0xBFF00000` to offset them.

## Workflow Loop

```
1. Write code
2. make clean && make           # Rebuild
3. make run-debug               # Launch QEMU with -s -S
4. In another terminal: gdb -x .gdbinit
5. Set breakpoints as needed
6. continue                     # Let QEMU run
7. Inspect, step, modify
8. quit                         # Exit GDB
9. Fix bugs, repeat
```

## Debugging Tips

- **Always** compile with `-g -O0` during development. Optimization makes debugging nearly impossible.
- Add a panic-on-unreachable: `if (0) { panic("unreachable reached"); }` liberally.
- Add assertion macros: `#define ASSERT(cond) if (!(cond)) { kernel_panic(__FILE__, __LINE__, "assertion failed: " #cond); }`
- Use the VGA buffer as a "poor man's printf" during early boot when no serial is available.
- Each subsystem should have a debug flag: `#define DEBUG_VMM 1` that enables extra VGA output.
- For hard-to-reproduce bugs, use QEMU's record/replay: `-rr snapshot` and `-rr replay=snapshot`.
