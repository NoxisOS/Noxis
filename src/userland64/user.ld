/* Linker script for 64-bit Noxis userland ELFs.
   Linked at 0x40000000 — outside the kernel's 128 MB identity map, so
   user pages can be mapped with the USER bit without colliding with the
   kernel's 2 MB identity pages. */

OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS {
    . = 0x40000000;

    .text   : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : { *(.bss*) *(COMMON) }

    /DISCARD/ : { *(.note*) *(.comment*) *(.eh_frame*) }
}
