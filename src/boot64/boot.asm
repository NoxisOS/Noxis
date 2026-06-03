; ─────────────────────────────────────────────────────────────
; src/boot64/boot.asm — Noxis x86_64 long-mode bring-up (milestone 1)
;
; BIOS loads this 512-byte boot sector at 0x7C00 in 16-bit real mode.
; Canonical path:  real mode → 32-bit protected mode → long mode.
;   1. enable A20
;   2. build 4-level page tables (identity-map first 2 MB via one 2 MB page)
;   3. load GDT (32-bit code, data, 64-bit code), set PE, far-jump to 32-bit
;   4. in 32-bit: enable PAE, load CR3, set EFER.LME, set CR0.PG
;   5. far-jump into the 64-bit code segment, print "NOXIS 64" to VGA
;
; Assemble:  nasm -f bin src/boot64/boot.asm -o build/boot64.img
; Run:       qemu-system-x86_64 -drive file=build/boot64.img,format=raw
; ─────────────────────────────────────────────────────────────

[BITS 16]
[ORG 0x7C00]

%define PML4   0x1000
%define PDPT   0x2000
%define PD     0x3000

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    ; ── A20 (fast gate via port 0x92) ─────────────────────────
    in   al, 0x92
    or   al, 2
    out  0x92, al

    ; ── Zero page tables (3 × 4 KB) ───────────────────────────
    mov  edi, PML4
    xor  eax, eax
    mov  ecx, 0x3000 / 4
    rep  stosd

    ; ── PML4[0] → PDPT, PDPT[0] → PD, PD[0] → 0 (2 MB page) ───
    mov  dword [PML4], PDPT | 0x3      ; present | rw
    mov  dword [PDPT], PD   | 0x3
    mov  dword [PD],   0x0  | 0x83     ; present | rw | PS (2 MB)

    lgdt [gdt_desc]

    ; ── Enter 32-bit protected mode (PE only) ─────────────────
    mov  eax, cr0
    or   eax, 1
    mov  cr0, eax
    jmp  0x08:pm32                     ; 16-bit far jump → 32-bit code

; ─────────────────────────────────────────────────────────────
[BITS 32]
pm32:
    mov  ax, 0x10                      ; 32-bit data segment
    mov  ds, ax
    mov  es, ax
    mov  ss, ax
    mov  esp, 0x7C00

    ; ── Enable PAE ────────────────────────────────────────────
    mov  eax, cr4
    or   eax, 1 << 5
    mov  cr4, eax

    ; ── CR3 → PML4 ────────────────────────────────────────────
    mov  eax, PML4
    mov  cr3, eax

    ; ── EFER.LME (long mode enable) ──────────────────────────
    mov  ecx, 0xC0000080
    rdmsr
    or   eax, 1 << 8
    wrmsr

    ; ── Enable paging → activates long mode ───────────────────
    mov  eax, cr0
    or   eax, 1 << 31
    mov  cr0, eax

    jmp  0x18:lm64                     ; 32-bit far jump → 64-bit code

; ─────────────────────────────────────────────────────────────
[BITS 64]
lm64:
    mov  ax, 0x10
    mov  ds, ax
    mov  es, ax
    mov  ss, ax
    mov  fs, ax
    mov  gs, ax

    mov  rdi, 0xB8000
    mov  rsi, msg
.print:
    lodsb
    test al, al
    jz   .halt
    mov  ah, 0x0F
    mov  [rdi], ax
    add  rdi, 2
    jmp  .print
.halt:
    hlt
    jmp  .halt

msg: db "NOXIS 64", 0

; ─────────────────────────────────────────────────────────────
; GDT: null, 32-bit code, data (flat 4 GB), 64-bit code (L=1)
align 8
gdt:
    dq 0x0000000000000000              ; 0x00 null
    dq 0x00CF9A000000FFFF              ; 0x08 code32 (exec/read, 4 GB)
    dq 0x00CF92000000FFFF              ; 0x10 data   (read/write, 4 GB)
    dq 0x00209A0000000000              ; 0x18 code64 (exec/read, L=1)
gdt_desc:
    dw gdt_desc - gdt - 1
    dd gdt

times 510 - ($ - $$) db 0
dw 0xAA55
