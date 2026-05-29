# Skill: Protected Mode

## Purpose
This skill covers transitioning the x86 CPU from 16-bit real mode to 32-bit protected mode, including segment setup, privilege levels, and the mental model of how addressing changes.

## Key Concepts

### Real Mode vs Protected Mode Addressing

| Aspect              | Real Mode                          | Protected Mode                       |
|---------------------|------------------------------------|--------------------------------------|
| Address width       | 20-bit (1 MB max)                  | 32-bit (4 GB)                        |
| Address calculation | `segment * 16 + offset`            | Segment selector → GDT entry → base  |
| Memory protection   | None                               | Ring 0-3, page-level protection      |
| Segment limits      | Always 64 KB                       | Configurable per segment (1 B – 4 GB)|
| Default operand size| 16-bit                             | 32-bit (with D/B flag in descriptor) |
| Interrupt handling  | IVT at 0x0000                      | IDT (anywhere, configurable)         |

### Control Register CR0

The bit that controls it all:

```
CR0 Bit 0 (PE) — Protection Enable
    0 = Real mode
    1 = Protected mode

CR0 Bit 31 (PG) — Paging
    0 = Paging disabled
    1 = Paging enabled (requires PE=1)

CR0 Bit 16 (WP) — Write Protect
    0 = Supervisor can write read-only pages
    1 = Read-only enforced at all privilege levels (must be set for proper COW)
```

### The Transition Procedure

This is the canonical, non-negotiable sequence for entering protected mode:

```
1. CLI                      ; disable interrupts (real mode IVT no longer valid)
2. LGDT [gdt_descriptor]    ; load GDT register
3. MOV EAX, CR0
   OR  EAX, 1               ; set PE bit
   MOV CR0, EAX             ; switch to protected mode
4. JMP 0x08:.pmode_entry    ; far jump to flush prefetch queue + load CS
5. Reload segment registers  ; DS, ES, FS, GS, SS → data segment selector
6. Set up stack              ; now in flat 32-bit mode
```

### Why the Far Jump?

After setting CR0.PE, the CPU is in protected mode for all **new** memory accesses. But the instruction prefetch queue still contains real-mode-decoded instructions. The far JMP (or far CALL) flushes the prefetch queue and loads CS with a valid protected-mode selector. Without this, the CPU executes garbage.

### GDT Layout for Transition

At minimum, you need a **null descriptor** (required by Intel) and at least two valid descriptors:

```nasm
; Temporary GDT for transition
gdt_start:
    dq 0x0000000000000000     ; null descriptor (index 0, mandatory)
    dq 0x00CF9A000000FFFF     ; 32-bit code segment, ring 0, base=0, limit=4 GB
    dq 0x00CF92000000FFFF     ; 32-bit data segment, ring 0, base=0, limit=4 GB
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1  ; size - 1
    dd gdt_start                 ; base address
```

The selectors are then:
- Code segment: `0x08` (index 1, RPL=0)
- Data segment: `0x10` (index 2, RPL=0)

## Common Pitfalls

1. **Not disabling interrupts before LGDT**: An interrupt arriving after CR0.PE is set but before the IDT is set up will triple-fault. Always CLI first.
2. **Wrong GDT base**: If `org` directive doesn't match the physical load address, GDT addresses are wrong and the CPU loads garbage selectors.
3. **Far-jump syntax**: In NASM with `[BITS 32]`, `jmp 0x08:label` works. With `[BITS 16]`, you need `jmp dword 0x08:label` for 32-bit targets.
4. **Not reloading all segment registers**: CS is reloaded by the far jump. DS, ES, FS, GS, SS must be reloaded manually with `mov ax, 0x10` + `mov ds, ax` etc.
5. **Stack pointer in low memory**: After switching, the stack must be in accessible memory (above 1 MB is fine with flat 4 GB segments). A stack at `0x7C00` will work but is fragile — move it to a dedicated location.
6. **Forgetting BITS directive**: NASM needs `[BITS 16]` for real-mode code and `[BITS 32]` for protected-mode code. Mixed directives in one file are fine.
7. **GDT limit field**: The limit field in the GDTR is `size - 1`, not the size. A 24-byte GDT has limit 23 (0x17). Wrong limit = #GP on segment reload.

## Debugging Tips

- Add an infinite loop (`hlt` + `jmp $`) RIGHT after the `mov cr0, eax` instruction and use `info registers` in GDB to verify CR0.PE is set
- After the far jump, write `'O'` to `0xB8000` to confirm execution reached the 32-bit code
- Verify segment registers: `print/x $cs`, `print/x $ds` should show the selector values
- Triple fault after setting CR0.PE usually means: GDT malformed, far-jump target wrong, or segments not reloaded
