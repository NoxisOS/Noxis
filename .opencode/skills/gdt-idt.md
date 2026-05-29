# Skill: GDT & IDT

## Purpose
This skill covers the Global Descriptor Table (GDT) and Interrupt Descriptor Table (IDT) — the two fundamental CPU tables that define memory segmentation and interrupt handling in protected mode.

## Key Concepts

### GDT (Global Descriptor Table)

Defines memory segments: their base address, limit, and access rights. Every memory access in protected mode goes through a segment descriptor.

**Descriptor Format (8 bytes):**
```
Bits 63-56  | 55-52 | 51-48 | 47-40  | 39-32 | 31-16 | 15-0
Base[31:24] | Flags | Limit[19:16] | Access | Base[23:0] | Limit[15:0]
```

**Access Byte (bits 47-40):**
```
Bit 7: Present (1 = valid descriptor)
Bit 6-5: DPL (Descriptor Privilege Level, 0-3)
Bit 4: System (0 = system, 1 = code/data)
Bit 3: Type bit 3
Bit 2: Type bit 2
Bit 1: Type bit 1
Bit 0: Accessed (CPU sets this)

Type bits for code/data (S=1):
  1010 = Execute/Read, accessed
  1011 = Execute/Read, conforming
  1100 = Execute/Read-Execute, conforming, accessed
  0010 = Read/Write, accessed
  0011 = Read/Write, expand-down
  0001 = Read/Write, accessed
```

**Flags (bits 55-52):**
```
Bit 3: Granularity (0 = byte, 1 = 4 KB pages)
Bit 2: Size (0 = 16-bit, 1 = 32-bit protected mode)
Bit 1: 64-bit code segment (ignored in 32-bit)
Bit 0: Available for OS use
```

### IDT (Interrupt Descriptor Table)

Defines handlers for exceptions and interrupts. The CPU uses the vector number (0-255) as an index into the IDT.

**Gate Descriptor Format (8 bytes):**
```
Bits 63-48: Offset[31:16]
Bits 47-45: Always 0
Bits 44-40: DPL + flags
Bits 39-32: Segment selector (CS)
Bits 31-16: Offset[15:0]
Bits 15-0:  Segment selector[15:0] — WAIT, no:
Bits 31-16: Offset[15:0]
Bits 15-0:  Reserved / meta

Correct layout:
[63-48] Offset[31:16]
[47-40] Flags (P=1, DPL, 0, Type[3:0])
[39-37] Reserved (must be 0)
[36-32] Reserved
[31-16] Segment selector
[15-0]  Offset[15:0]
```

Actually, let me be precise:

```
Offset[31:16]  : bits 63-48
Flags          : bits 47-40
  Bit 7 (47): Present (1)
  Bit 6-5: DPL (ring level required to call this gate via INT)
  Bit 4: Always 0 for interrupt/trap gates
  Bit 3-0: Gate type
    0101 (0x5) = 32-bit Task Gate
    0110 (0x6) = 16-bit Interrupt Gate
    0111 (0x7) = 16-bit Trap Gate
    1110 (0xE) = 32-bit Interrupt Gate (clears IF on entry)
    1111 (0xF) = 32-bit Trap Gate (preserves IF)
Reserved       : bits 39-32 (must be 0)
Segment Selector: bits 31-16 (CS, usually 0x08)
Offset[15:0]   : bits 15-0
```

**Interrupt Gate vs Trap Gate:**
- Interrupt Gate (0x8E): Clears EFLAGS.IF on entry → disables maskable interrupts. Use for critical handlers.
- Trap Gate (0x8F): Preserves EFLAGS.IF → allows nested interrupts. Use for IRQ handlers to allow higher-priority IRQs.

### PIC (8259A Programmable Interrupt Controller)

Two cascaded PICs: Master at ports 0x20-0x21, Slave at ports 0xA0-0xA1.

**Remapping:** The PIC defaults to mapping IRQ0 to vector 0x08, which overlaps with CPU exceptions. We must remap:
- Master: IRQ0-7 → vectors 0x20-0x27
- Slave: IRQ8-15 → vectors 0x28-0x2F

Canonical remapping sequence:
```c
/* ICW1: Start initialization */
port_byte_out(0x20, 0x11);  /* Master */
port_byte_out(0xA0, 0x11);  /* Slave */

/* ICW2: Base vector */
port_byte_out(0x21, 0x20);  /* Master: vector 0x20 */
port_byte_out(0xA1, 0x28);  /* Slave: vector 0x28 */

/* ICW3: Cascade config */
port_byte_out(0x21, 0x04);  /* Master: IRQ2 has slave */
port_byte_out(0xA1, 0x02);  /* Slave: cascaded to master IRQ2 */

/* ICW4: Mode */
port_byte_out(0x21, 0x01);  /* Master: 8086 mode */
port_byte_out(0xA1, 0x01);  /* Slave: 8086 mode */
```

## Common Pitfalls

1. **IDT limit**: Like GDT, the limit is `size - 1`. A 256-entry IDT is 2048 bytes → limit 2047 (0x7FF).
2. **Segment selector in IDT gates**: Must be a valid GDT selector, usually `0x08` (ring 0 code). Using `0x00` (null selector) causes #GP.
3. **Forgetting to remap PIC**: If PIC is not remapped, IRQ0 (timer) fires on vector 0x08 which is the double-fault handler. Instant chaos.
4. **Not sending EOI**: After handling an IRQ, the PIC must receive an End-Of-Interrupt (EOI) signal: `outb(0x20, 0x20)` for master, `outb(0xA0, 0x20)` then `outb(0x20, 0x20)` for slave IRQs.
5. **Masking the slave's cascade**: The slave is connected to master IRQ2. Masking IRQ2 on the master disables ALL slave IRQs (8-15).
6. **Ring 3 code segment**: DPL must be 3 for user mode code and data segments. Ring 0 segments have DPL=0. Trying to execute ring 3 code with a ring 0 CS causes #GP.
7. **GDT descriptor flags**: Granularity bit set (4 KB pages) × limit 0xFFFFF = 4 GB. Granularity clear (bytes) × limit 0xFFFFF = 1 MB. Classic mix-up.
8. **TSS not set up**: When jumping to ring 3, the CPU uses the TSS to find the ring 0 stack. Without a valid TSS, the switch fails with #TS (Triple fault in practice).

## Implementation Pattern

```c
/**
 * @brief Sets an IDT gate entry
 * @param vector  Interrupt vector number (0-255)
 * @param handler Base address of the handler function
 * @param flags   Gate type and DPL (e.g., 0x8E for ring 0 interrupt gate)
 * @return OS_OK on success, OS_ERR_INVALID if vector > 255
 */
os_status_t idt_set_gate(uint8_t vector, uint32_t handler, uint8_t flags) {
    if (vector > 255) return OS_ERR_INVALID;

    g_idt[vector].offset_low  = handler & 0xFFFF;
    g_idt[vector].offset_high = (handler >> 16) & 0xFFFF;
    g_idt[vector].selector    = 0x08;  /* Kernel code segment */
    g_idt[vector].zero        = 0;
    g_idt[vector].flags       = flags;
    return OS_OK;
}
```

## Debugging Tips

- Use `info gdt` in GDB/QEMU monitor to dump the GDT
- Use `info idt` in QEMU monitor to dump the IDT
- Triple fault after enabling interrupts: check that the IDT is loaded (LIDT executed), gates point to valid code, and the PIC is remapped
- Spurious IRQ7 or IRQ15: these are normal — the PIC uses them to signal "no interrupt pending." Handle them gracefully (just send EOI, don't panic)
- Double fault (vector 8): usually a page fault (vector 14) handler itself faults, or a stack overflow during exception handling
