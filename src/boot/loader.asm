; ─────────────────────────────────────────────────────────────
; boot/loader.asm — Bootloader: A20, kernel load, protected mode
;
; Purpose: Enable A20 gate, load kernel from disk to buffer
;          below 1 MB, switch to protected mode, copy kernel
;          to 0x100000, then jump to kernel entry.
;
; Entry:  MBR jumps here at 0x0000:0x7E00.
;         DL = boot drive number.
;
; Exit:   Jumps to kernel at 0x100000 (32-bit protected mode).
;         Never returns.
;
; Register convention:
;   Preserved: DL (boot drive), later stored to [drive_number]
;   16-bit: freely use AX, BX, CX, SI, DI
;   32-bit: freely use EAX, EBX, ECX, EDX, ESI, EDI
; ─────────────────────────────────────────────────────────────

%include "defines.asm"

section .text
[BITS 16]
[ORG LOADER_ORIGIN]

; ── Entry: short jump past magic signature ───────────────────
    jmp  short .real_start          ; 2 bytes

    ; Magic signature at offset 2 — verified by MBR
    dw   0xDEAD                      ; 2 bytes at offset 2

.real_start:
    ; Store boot drive number passed in DL
    mov  [drive_number], dl

    ; Set up segments and stack for real mode
    xor  ax, ax
    mov  ds, ax
    mov  es, ax
    mov  ss, ax
    mov  sp, LOADER_ORIGIN          ; stack right below Stage 2

    ; ── Enable A20 gate ──────────────────────────────────────
    call _enable_a20

    ; ── Load kernel from disk to buffer ──────────────────────
    call _load_kernel

    ; ── Enter protected mode ─────────────────────────────────
    cli                             ; disable interrupts (IVT is invalid in PM)

    ; Load GDT
    lgdt [gdt_descriptor]

    ; Set PE (Protection Enable) bit in CR0
    mov  eax, cr0
    or   eax, 1
    mov  cr0, eax

    ; Far jump to flush prefetch queue and load 32-bit CS
    jmp  KERNEL_CS:.pmode_entry

; ── 32-bit protected mode entry ──────────────────────────────
[BITS 32]
.pmode_entry:
    ; Reload all segment registers with kernel data selector
    mov  ax, KERNEL_DS
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax

    ; Set up a temporary stack in low memory (128 KB, below 1 MB)
    mov  esp, 0x20000

    ; ── Copy kernel from load buffer to 1 MB ─────────────────
    ; The kernel was loaded below 1 MB (at KERNEL_LOAD_ADDR)
    ; because real mode INT 0x13 cannot access addresses >= 1 MB.
    ; Now in protected mode, we copy it up to KERNEL_TARGET_ADDR.
    mov  esi, KERNEL_LOAD_ADDR
    mov  edi, KERNEL_TARGET_ADDR
    mov  ecx, [kernel_size]         ; bytes loaded (sectors * 512)
    shr  ecx, 2                     ; divide by 4 for dword copy
    cld
    rep  movsd

    ; ── Jump to kernel entry ─────────────────────────────────
    jmp  KERNEL_TARGET_ADDR

    ; Should never reach here
    cli
    hlt

; ── Enable A20 gate ──────────────────────────────────────────
; Attempts two methods: Fast A20 (port 0x92), then keyboard controller.
; Input:  None
; Output: A20 enabled, or hangs if all methods fail
; Destroys: AX
[BITS 16]
_enable_a20:
    ; Method 1: Fast A20 via System Control Port A (0x92)
    in   al, 0x92
    test al, 2                      ; bit 1 = A20 gate already active?
    jnz  .a20_done
    or   al, 2                      ; set A20 gate enable (bit 1)
    and  al, 0xFE                   ; clear system reset (bit 0)
    out  0x92, al
    ; Re-check after setting
    in   al, 0x92
    test al, 2
    jnz  .a20_done

    ; Method 2: Keyboard controller (ports 0x64, 0x60)
    ; Write output port command (0xD1), then set A20 bit (0xDF)
    call .kbc_wait_inbuf
    mov  al, 0xD1                   ; write output port command
    out  0x64, al
    call .kbc_wait_inbuf
    mov  al, 0xDF                   ; A20 gate on, all other bits set
    out  0x60, al
    call .kbc_wait_inbuf

.a20_done:
    ret

; Wait for keyboard controller input buffer to be empty (bit 1 = 0)
.kbc_wait_inbuf:
    in   al, 0x64
    test al, 2
    jnz  .kbc_wait_inbuf
    ret

; ── Load kernel from disk ────────────────────────────────────
; Loads KERNEL_SECTORS from disk to KERNEL_LOAD_ADDR.
; Prefers LBA extended read (INT 0x13 AH=0x42), falls back to CHS.
; Input:  DL = drive number
; Output: [kernel_size] = bytes loaded (KERNEL_SECTORS * 512)
; Destroys: AX, BX, CX, DX, SI
[BITS 16]
_load_kernel:
    ; Set ES to kernel load segment for CHS fallback
    mov  ax, KERNEL_LOAD_SEG
    mov  es, ax

    ; ── Check for LBA BIOS extensions (INT 0x13 AH=0x41) ─────
    mov  ah, 0x41
    mov  bx, 0x55AA
    mov  dl, [drive_number]
    int  0x13
    jc   .use_chs                   ; not supported
    cmp  bx, 0xAA55
    jne  .use_chs                   ; signature not preserved

    ; ── Extended read (INT 0x13 AH=0x42) with DAP ────────────
    ; Build Disk Address Packet on stack (16 bytes)
    push dword 0                    ; LBA[32-63] — always 0
    push dword KERNEL_SECTOR        ; LBA[0-31] — starting sector
    push dword KERNEL_LOAD_SEG      ; buffer segment
    push dword KERNEL_LOAD_OFF      ; buffer offset
    push dword KERNEL_SECTORS       ; transfer size in sectors
    push dword 0x0010               ; packet size = 16 bytes
    mov  ah, 0x42                   ; extended read
    mov  dl, [drive_number]
    mov  si, sp                     ; DS:SI → DAP
    int  0x13
    jc   .disk_error
    add  sp, 24                     ; clean DAP off stack
    jmp  .calc_size

    ; ── Fallback: CHS read (INT 0x13 AH=0x02) ────────────────
.use_chs:
    mov  ah, 0x02                   ; read sectors
    mov  al, KERNEL_SECTORS
    mov  ch, 0                      ; cylinder 0
    mov  cl, KERNEL_SECTOR + 1      ; sector = LBA + 1
    mov  dh, 0                      ; head 0
    mov  dl, [drive_number]
    mov  bx, KERNEL_LOAD_OFF        ; ES:BX → buffer (ES set above)
    int  0x13
    jc   .disk_error

.calc_size:
    mov  eax, KERNEL_SECTORS
    shl  eax, 9                     ; * 512 → bytes
    mov  [kernel_size], eax
    ret

.disk_error:
    mov  si, disk_error_msg
    call _print_string_16
    cli
    hlt

; ── Print null-terminated string in real mode ────────────────
[BITS 16]
_print_string_16:
    lodsb                           ; AL = [DS:SI], SI++
    or   al, al
    jz   .done_16
    mov  ah, 0x0E                   ; BIOS teletype
    int  0x10
    jmp  _print_string_16
.done_16:
    ret

; ── Data ─────────────────────────────────────────────────────
drive_number   db 0
disk_error_msg db "ERR:KERNEL_DISK", 0
kernel_size    dd 0

; ── Temporary GDT (flat memory model, 4 GB segments) ─────────
; Used only to enter protected mode. The kernel will set up its
; own permanent GDT later.
align 8
gdt_start:
    ; Null descriptor (index 0) — required by Intel spec
    dq 0x0000000000000000

    ; 32-bit code segment (index 1): base=0, limit=4 GB, DPL=0, exec/read
    ; dword 0: limit[15:0]=0xFFFF, base[15:0]=0x0000
    ; dword 1: base[31:24]=0x00, flags=0xC, limit[19:16]=0xF, access=0x9A, base[23:16]=0x00
    dd 0x0000FFFF
    dd 0x00CF9A00

    ; 32-bit data segment (index 2): base=0, limit=4 GB, DPL=0, read/write
    dd 0x0000FFFF
    dd 0x00CF9200
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1     ; limit = size - 1
    dd gdt_start                    ; base address
