/**
 * @file    src/boot64/isr.c
 * @brief   C-side exception dispatcher for the 64-bit kernel.
 */
#include "types.h"

void serial_write(const char* s);
void serial_hex(uint64_t v);

static const char* const _names[32] = {
    "Divide Error","Debug","NMI","Breakpoint","Overflow","Bound Range",
    "Invalid Opcode","Device Not Available","Double Fault","Coproc Segment",
    "Invalid TSS","Segment Not Present","Stack Fault","General Protection",
    "Page Fault","Reserved","x87 FP","Alignment Check","Machine Check",
    "SIMD FP","Virtualization","Control Protection","Reserved","Reserved",
    "Reserved","Reserved","Reserved","Reserved","Hypervisor","VMM Comm",
    "Security","Reserved"
};

void isr_dispatch(uint64_t vec, uint64_t err) {
    serial_write("\n[noxis64] EXCEPTION ");
    serial_hex(vec);
    if (vec < 32) { serial_write("  "); serial_write(_names[vec]); }
    serial_write("  err="); serial_hex(err);
    serial_write("\n");

    uint64_t cr2;
    __asm__ __volatile__("mov %%cr2, %0" : "=r"(cr2));
    serial_write("[noxis64] CR2="); serial_hex(cr2); serial_write("\n");

    serial_write("[noxis64] halted.\n");
    for (;;) __asm__ __volatile__("cli; hlt");
}
