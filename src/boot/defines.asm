; ─────────────────────────────────────────────────────────────
; boot/defines.asm — Constants shared between MBR and loader
; ─────────────────────────────────────────────────────────────

; ── Disk layout ──────────────────────────────────────────────
%define MBR_SECTOR           0       ; MBR at sector 0
%define LOADER_SECTOR        1       ; Loader starts at LBA 1
%define LOADER_SECTORS       4       ; Loader size in sectors (2 KB)
%define KERNEL_SECTOR        5       ; Kernel binary starts at LBA 5
%define KERNEL_SECTORS       64      ; Kernel max size (32 KB)
%define KERNEL_LOAD_ADDR     0x10000 ; Temp load buffer (below 1 MB)
%define KERNEL_LOAD_SEG      0x1000  ; Segment for KERNEL_LOAD_ADDR
%define KERNEL_LOAD_OFF      0x0000  ; Offset for KERNEL_LOAD_ADDR
%define KERNEL_TARGET_ADDR   0x100000; Final kernel location (1 MB)

; ── Memory addresses ─────────────────────────────────────────
%define MBR_ORIGIN           0x7C00  ; BIOS loads MBR here
%define MBR_RELOC            0x0600  ; MBR relocates here
%define LOADER_ORIGIN        0x7E00  ; Loader loaded here
%define VGA_BUFFER           0xB8000  ; VGA text mode buffer

; ── Segment selectors (after GDT is loaded) ──────────────────
%define KERNEL_CS            0x08    ; Ring 0 code segment
%define KERNEL_DS            0x10    ; Ring 0 data segment

; ── GDT descriptor flags ─────────────────────────────────────
; Access byte: Present=1, DPL=0, S=1 (code/data)
;   Code: Exec=1, Conforming=0, Readable=1, Accessed=0 → 0x9A
;   Data: Exec=0, Direction=0, Writable=1, Accessed=0  → 0x92
; Flags: Granularity=1 (4KB), Size=1 (32-bit), Long=0   → 0xCF
%define GDT_CODE_FLAGS       0x9A
%define GDT_DATA_FLAGS       0x92
%define GDT_FLAGS            0xCF
