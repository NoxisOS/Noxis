/* ─────────────────────────────────────────────────────────────
 * linker.ld — Noxis x86_64 higher-half kernel link script.
 *
 * The kernel is loaded at PHYSICAL 0x10000 by the boot sector, but
 * LINKED at the higher-half virtual base.  The boot maps
 * 0xFFFFFFFF80000000 → physical 0, so 0xFFFFFFFF80010000 → 0x10000.
 * Entry: _start (kernel_entry.asm) — a low→high trampoline.
 * ─────────────────────────────────────────────────────────── */

OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

KERNEL_VBASE = 0xFFFFFFFF80010000;

SECTIONS
{
    . = KERNEL_VBASE;

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
        *(.comment) *(.eh_frame) *(.note*) *(.iplt) *(.igot) *(.rel.*)
    }
}
