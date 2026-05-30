/* Shared linker script for all Noxis userland ELFs.
   One PT_LOAD at 0x400000, BSS zeroed by elf_load. */

ENTRY(_start)

SECTIONS {
    . = 0x400000;
    .text   : { *(.text*)   }
    .data   : { *(.data*)  *(.rodata*) }
    .bss    : { *(.bss*)   *(COMMON) }
    /DISCARD/ : { *(.note*) *(.comment*) *(.eh_frame*) }
}
