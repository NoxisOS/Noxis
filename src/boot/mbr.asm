; ─────────────────────────────────────────────────────────────
; boot/mbr.asm — Master Boot Record (512 bytes at 0x7C00)
;
; Purpose: First sector loaded by BIOS. Relocates to 0x0600,
;          reads loader.asm from disk to 0x7E00, chains to it.
;
; Entry:  BIOS loads this at 0x7C00 in real mode.
;         DL = boot drive number.
;
; Exit:   Jumps to loader at 0x0000:0x7E00.
;         DL still holds boot drive number.
;
; Register convention:
;   Preserved across calls: DL (boot drive)
;   Destroyed freely: AX, BX, CX, SI, DI
; ─────────────────────────────────────────────────────────────

%include "defines.asm"

section .text
[BITS 16]
[ORG MBR_ORIGIN]

; ── Entry point ──────────────────────────────────────────────
_start:
    cli                             ; disable interrupts during setup
    xor  ax, ax
    mov  ds, ax                     ; DS = 0
    mov  es, ax                     ; ES = 0
    mov  ss, ax                     ; SS = 0
    mov  sp, MBR_ORIGIN          ; stack below MBR

    mov  bp, sp                     ; frame pointer

    ; Save boot drive number (BIOS passes it in DL)
    mov  [drive_number], dl

    ; ── Relocate MBR to 0x0600 ───────────────────────────────
    ; We must relocate because loader will be loaded at 0x7E00
    ; which overlaps with 0x7C00 + 512 bytes.
    mov  si, MBR_ORIGIN          ; source: 0x7C00
    mov  di, MBR_RELOC           ; dest:   0x0600
    mov  cx, 512                    ; 512 bytes
    cld                             ; forward direction
    rep  movsb                      ; copy

    ; Far jump to the relocated code to reset CS:IP
    jmp  0x0000:.relocated

; ── Relocated execution continues here ───────────────────────
.relocated:
    ; Re-set segment registers (CS was set by the jump)
    xor  ax, ax
    mov  ds, ax
    mov  es, ax
    mov  ss, ax
    mov  sp, MBR_ORIGIN

    ; Reset disk system (some BIOSes need this after boot)
    mov  ah, 0x00                   ; reset disk
    mov  dl, [drive_number]
    int  0x13
    jc   .disk_error                ; unlikely but defensive

    ; ── Load loader from disk to 0x7E00 ───────────────────────
    mov  ah, 0x02                   ; read sectors
    mov  al, LOADER_SECTORS         ; number of sectors
    mov  ch, 0                      ; cylinder 0
    mov  cl, LOADER_SECTOR + 1      ; sector (CHS sector = LBA + 1)
    mov  dh, 0                      ; head 0
    mov  dl, [drive_number]
    mov  bx, LOADER_ORIGIN          ; ES:BX = 0x0000:0x7E00
    int  0x13
    jc   .disk_error

    ; Verify magic signature in loader (at offset 2, past jmp)
    cmp  word [LOADER_ORIGIN + 2], 0xDEAD
    jne  .loader_error

    ; ── Jump to loader ───────────────────────────────────────
    mov  dl, [drive_number]         ; pass boot drive to loader
    jmp  0x0000:LOADER_ORIGIN

; ── Error handlers ───────────────────────────────────────────
.disk_error:
    mov  si, disk_error_msg
    call _print_string
    cli
    hlt

.loader_error:
    mov  si, loader_error_msg
    call _print_string
    cli
    hlt

; ── Print string in SI using BIOS teletype ───────────────────
; Input:  DS:SI = pointer to null-terminated string
; Output: None
; Destroys: AX, SI
_print_string:
    lodsb                           ; load byte from [SI] into AL, SI++
    or   al, al
    jz   .done
    mov  ah, 0x0E                   ; BIOS teletype function
    int  0x10
    jmp  _print_string
.done:
    ret

; ── Data ─────────────────────────────────────────────────────
disk_error_msg     db "ERR:DISK", 0
loader_error_msg   db "ERR:LOADER", 0
drive_number       db 0

; ── MBR signature ────────────────────────────────────────────
times 510 - ($ - $$) db 0          ; pad to 510 bytes
dw 0xAA55                           ; MBR magic word
