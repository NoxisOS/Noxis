; ─────────────────────────────────────────────────────────────
; src/boot64/boot.asm — Noxis x86_64 boot sector.
;
; BIOS loads this 512-byte sector at 0x7C00 (16-bit real mode).
;   1. save boot drive, enable A20
;   2. load the 64-bit kernel from disk (LBA 1..N) to 0x10000 via INT 13h/42h
;   3. build 4-level page tables (identity-map first 2 MB, 2 MB page)
;   4. GDT + PE → 32-bit protected mode
;   5. PAE + EFER.LME + PG → long mode
;   6. far-jump to 64-bit, then jump into the kernel at 0x10000
;
; Assemble:  nasm -f bin src/boot64/boot.asm -o build/boot64_mbr.bin
; ─────────────────────────────────────────────────────────────

[BITS 16]
[ORG 0x7C00]

%define PML4        0x1000
%define PDPT        0x2000
%define PD          0x3000
%define KERNEL_SEG  0x1000        ; 0x1000:0x0000 = phys 0x10000
%define KERNEL_LBA  1             ; kernel starts at sector 1 (0-based LBA)
%define KERNEL_SECS 64            ; load 64 sectors (32 KB) — plenty

start:
    cli
    xor  ax, ax
    mov  ds, ax
    mov  es, ax
    mov  ss, ax
    mov  sp, 0x7C00
    mov  [boot_drive], dl         ; BIOS leaves boot drive in DL

    ; ── A20 ───────────────────────────────────────────────────
    in   al, 0x92
    or   al, 2
    out  0x92, al

    ; ── Load kernel via INT 13h extended read (LBA) ──────────
    mov  si, dap
    mov  ah, 0x42
    mov  dl, [boot_drive]
    int  0x13
    jc   disk_err

    ; ── Page tables: PML4 → PDPT → PD (2 MB page at 0) ───────
    mov  edi, PML4
    xor  eax, eax
    mov  ecx, 0x3000 / 4
    rep  stosd
    mov  dword [PML4], PDPT | 0x3
    mov  dword [PDPT], PD   | 0x3
    mov  dword [PD],   0x0  | 0x83

    lgdt [gdt_desc]

    mov  eax, cr0
    or   eax, 1
    mov  cr0, eax
    jmp  0x08:pm32

disk_err:
    mov  si, msg_err
.l: lodsb
    test al, al
    jz   .h
    mov  ah, 0x0E
    int  0x10
    jmp  .l
.h: hlt
    jmp  .h

; ─────────────────────────────────────────────────────────────
[BITS 32]
pm32:
    mov  ax, 0x10
    mov  ds, ax
    mov  es, ax
    mov  ss, ax
    mov  esp, 0x7C00

    mov  eax, cr4
    or   eax, 1 << 5              ; PAE
    mov  cr4, eax

    mov  eax, PML4
    mov  cr3, eax

    mov  ecx, 0xC0000080         ; EFER
    rdmsr
    or   eax, 1 << 8             ; LME
    wrmsr

    mov  eax, cr0
    or   eax, 1 << 31            ; PG
    mov  cr0, eax

    jmp  0x18:lm64

; ─────────────────────────────────────────────────────────────
[BITS 64]
lm64:
    mov  ax, 0x10
    mov  ds, ax
    mov  es, ax
    mov  ss, ax
    mov  fs, ax
    mov  gs, ax

    mov  rax, 0x10000            ; kernel entry (_start64 linked here)
    jmp  rax

; ── Disk Address Packet for INT 13h/42h ──────────────────────
align 4
dap:
    db 0x10                       ; size
    db 0                          ; reserved
    dw KERNEL_SECS                ; sectors to read
    dw 0x0000                     ; dest offset
    dw KERNEL_SEG                 ; dest segment
    dq KERNEL_LBA                 ; starting LBA

boot_drive: db 0
msg_err:    db "DISK ERR", 0

; ── GDT: null, 32-bit code, data, 64-bit code (L=1) ──────────
align 8
gdt:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF        ; 0x08 code32
    dq 0x00CF92000000FFFF        ; 0x10 data
    dq 0x00209A0000000000        ; 0x18 code64
gdt_desc:
    dw gdt_desc - gdt - 1
    dd gdt

times 510 - ($ - $$) db 0
dw 0xAA55
