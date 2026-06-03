/* ─────────────────────────────────────────────────────────────
 * linker.ld — Noxis x86_64 kernel link script.
 *
 * Links the kernel flat at physical 0x100000 (1 MB), identity-mapped
 * by the long-mode boot sector.  Higher-half relocation is a later
 * refinement.  Entry: _start (kernel_entry.asm, 64-bit).
 * ─────────────────────────────────────────────────────────── */

OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0x10000;

    /* Entry stub must sit first in the binary. */
    .text BLOCK(4K) : {
        KEEP(*(.text.entry))
        *(.text*)
    }

    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }

    .bss : {
        _bss_start = .;
        *(COMMON)
        *(.bss*)
        _bss_end = .;
    }

    _kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
        *(.note*)
        *(.iplt)
        *(.igot)
        *(.rel.*)
    }
}
