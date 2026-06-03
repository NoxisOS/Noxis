/* Linker script for the 64-bit Noxis kernel (milestone: loaded low at
   0x10000, identity-mapped by the boot sector). entry.o must link first
   so _start64 sits at the very start of the image. */

ENTRY(_start64)

SECTIONS {
    . = 0x10000;

    .text   : { *(.text._start) *(.text*) }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : { *(.bss*) *(COMMON) }

    /DISCARD/ : { *(.note*) *(.comment*) *(.eh_frame*) }
}
