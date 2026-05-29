# Skill: Bootloader

## Purpose
This skill covers writing a custom 2-stage bootloader in NASM for the Noxis OS. No GRUB, no Multiboot — every byte from MBR to protected mode is ours.

## Key Concepts

### Stage 1 (MBR — 512 bytes at 0x7C00)
- BIOS loads exactly 512 bytes from the first sector to `0x00007C00`
- Must end with the magic word `0xAA55` at offset 510
- Real mode: 16-bit, segmented addressing, no paging, limited to ~1 MB
- Primary job: relocate itself, load Stage 2 from disk, jump to Stage 2

### Stage 2 (loaded by Stage 1)
- Larger (up to ~480 KB in low memory below 0x80000)
- Jobs: enable A20 line, detect memory (BIOS INT 0x15 E820), load kernel from disk to 0x100000, set up temporary GDT, switch to protected mode, far-jump to kernel entry

### Memory Map During Boot
```
0x00007C00 - Stage 1 (MBR) loaded by BIOS
0x00000600 - Stage 1 relocates here (standard convention, avoids overwrite)
0x00007E00 - Stage 2 loaded here (immediately after MBR)
0x00010000 - Kernel loaded here (above all BIOS areas)
```

### A20 Gate
- Legacy IBM PC compatibility: address bit 20 is masked by default
- Must be enabled to access memory above 1 MB
- Methods: keyboard controller (port 0x64), BIOS INT 0x15 AX=0x2401, Fast A20 (port 0x92)
- Recommended: try Fast A20 first (port 0x92), fallback to keyboard controller

### Disk Reading (BIOS INT 0x13)
- AH=0x02: Read sectors from disk
- Uses CHS (Cylinder/Head/Sector) addressing
- DL contains drive number (0x00 = floppy, 0x80 = first HDD)
- Limited to ~8 GB by BIOS, but sufficient for our kernel (< 1 MB)

## Common Pitfalls

1. **Origin directive wrong**: Stage 1 must use `org 0x7C00`, Stage 2 `org 0x7E00`. Wrong origin = wrong label addresses = crash.
2. **Segment registers uninitialized**: Real mode addressing is `segment:offset`. Set DS, ES, SS explicitly. Never assume BIOS left them at a useful value.
3. **A20 not enabled**: Kernel loaded above 1 MB will read back as address modulo 1 MB (address bit 20 cleared). Triple fault or garbage execution.
4. **Reading too many sectors**: BIOS INT 0x13 can read at most 128 sectors per call on some BIOS versions (and 18 per call on floppy). Loop for large reads.
5. **Stack overlapping code/data**: Set a stack well away from code. Typical: `0x9000:0xFFFF` (sp = 0xFFFF, ss = 0x9000).
6. **Not preserving DL**: BIOS passes the boot drive number in DL. Stage 1 must save it and pass it to Stage 2 for disk reads.
7. **DAP vs CHS**: Modern BIOSes support LBA via Extended Read (AH=0x42), but for maximum compatibility use CHS (AH=0x02) with LBA→CHS conversion.

## Implementation Pattern

```nasm
; stage1.asm — MBR: loads Stage 2, nothing else
section .text
[BITS 16]
[ORG 0x7C00]

_start:
    cli                     ; disable interrupts during setup
    xor  ax, ax
    mov  ds, ax             ; DS = 0
    mov  es, ax             ; ES = 0
    mov  ss, ax             ; SS = 0
    mov  sp, 0x7C00         ; stack grows down from MBR

    ; Relocate MBR to 0x0600
    mov  si, 0x7C00
    mov  di, 0x0600
    mov  cx, 512
    rep  movsb
    jmp  0x0000:.relocated  ; far jump to relocated code

.relocated:
    ; Save boot drive
    mov  [drive_number], dl

    ; Load Stage 2 from disk → 0x7E00
    ; ... INT 0x13 calls ...

    ; Jump to Stage 2
    jmp  0x0000:0x7E00

drive_number db 0

times 510 - ($ - $$) db 0   ; pad to 510 bytes
dw 0xAA55                   ; MBR magic
```

## Debugging Tips

- Use `-d cpu_reset,int` with QEMU to trace BIOS→MBR handoff
- Add `hlt` + `jmp $` loops at decision points, inspect with GDB via `-s -S`
- Print a character to VGA memory at `0xB8000` (`mov byte [0xB8000], 'X'`) to confirm code is executing
- Verify A20 with: write 0x00 to 0x100000, write 0xFF to 0x000000, read 0x100000. If it reads 0xFF, A20 is disabled (wrap-around)
- Use `info registers` in GDB to check segment registers and instruction pointer
