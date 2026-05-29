/* ─────────────────────────────────────────────────────────────
 * linker.ld — Linker script for the Noxis kernel
 *
 * Links the kernel at physical address 0x100000 (1 MB).
 * The bootloader loads the flat binary image here.
 *
 * Entry point: _start (in kernel_entry.asm)
 * ─────────────────────────────────────────────────────────── */

ENTRY(_start)

SECTIONS
{
    . = 0xC0100000;

    /* ── Entry point (must be first in binary) ──────────── */
    .text BLOCK(4K) : {
        KEEP(*(.text.entry))
        *(.text*)
    }

    /* ── Read-only data ────────────────────────────────── */
    .rodata : {
        *(.rodata*)
    }

    /* ── Initialized data ──────────────────────────────── */
    .data : {
        *(.data*)
    }

    /* ── BSS (uninitialized data) ──────────────────────── */
    .bss : {
        _bss_start = .;
        *(COMMON)
        *(.bss*)
        _bss_end = .;
    }

    /* ── Kernel end marker ─────────────────────────────── */
    _kernel_end = .;

    /* ── Discard unnecessary sections ──────────────────── */
    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
        *(.note*)
        *(.iplt)
        *(.igot)
        *(.rel.*)
    }
}
