/* Userland linker script — one PT_LOAD at 0x400000.
   elf_load only honors PT_LOAD; everything else is discarded. */

ENTRY(_start)

SECTIONS {
    . = 0x400000;

    .text : {
        *(.text*)
    }

    .data : {
        *(.data*)
        *(.rodata*)
    }

    .bss : {
        *(.bss*)
        *(COMMON)
    }

    /DISCARD/ : {
        *(.note*)
        *(.comment*)
        *(.eh_frame*)
    }
}
