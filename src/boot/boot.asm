; ─────────────────────────────────────────────────────────────
; src/boot/boot.asm — Noxis x86_64 boot sector.
;
; BIOS loads this 512-byte sector at 0x7C00 (16-bit real mode).
;   1. save boot drive, enable A20
;   2. load the 64-bit kernel from disk (LBA 1..N) to 0x10000 (INT 13h/42h)
;   3. build 4-level page tables, identity-map first 1 GB (2 MB pages)
;   4. GDT + PE → 32-bit protected mode
;   5. PAE + EFER.LME + PG → long mode
;   6. far-jump to 64-bit, jump into the kernel at 0x10000
; ─────────────────────────────────────────────────────────────

[BITS 16]
[ORG 0x7C00]

%define PML4        0x1000
%define PDPT        0x2000
%define PD          0x3000
%define KERNEL_SEG  0x1000        ; 0x1000:0x0000 = phys 0x10000
%define KERNEL_LBA  1
%define KERNEL_SECS 64            ; load 32 KB (kernel is ~6 KB; BIOS caps at 127)

start:
    cli
    xor  ax, ax
    mov  ds, ax
    mov  es, ax
    mov  ss, ax
    mov  sp, 0x7C00
    mov  [boot_drive], dl

    in   al, 0x92                 ; A20
    or   al, 2
    out  0x92, al

    mov  si, dap                  ; load kernel (LBA extended read)
    mov  ah, 0x42
    mov  dl, [boot_drive]
    int  0x13
    jc   disk_err

    ; ── Page tables: identity-map first 1 GB with 2 MB pages ──
    mov  edi, PML4
    xor  eax, eax
    mov  ecx, 0x3000 / 4
    rep  stosd
    mov  dword [PML4], PDPT | 0x3
    mov  dword [PDPT], PD   | 0x3
    mov  edi, PD
    mov  eax, 0x83                ; phys 0, present|rw|PS
    mov  ecx, 512
.fill_pd:
    mov  [edi], eax
    add  eax, 0x200000
    add  edi, 8
    loop .fill_pd

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

[BITS 32]
pm32:
    mov  ax, 0x10
    mov  ds, ax
    mov  es, ax
    mov  ss, ax
    mov  esp, 0x7C00

    mov  eax, cr4
    or   eax, 1 << 5             ; PAE
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

[BITS 64]
lm64:
    mov  ax, 0x10
    mov  ds, ax
    mov  es, ax
    mov  ss, ax
    mov  fs, ax
    mov  gs, ax
    mov  rax, 0x10000
    jmp  rax

align 4
dap:
    db 0x10
    db 0
    dw KERNEL_SECS
    dw 0x0000
    dw KERNEL_SEG
    dq KERNEL_LBA
boot_drive: db 0
msg_err:    db "DISK ERR", 0

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
