/**
 * @file    kernel/isr/isr.c
 * @brief   64-bit ISR dispatcher — routes exceptions/IRQs to handlers.
 */
#include <kernel/isr/isr.h>
#include <kernel/hal/pic.h>
#include <drivers/serial.h>

void serial_write_hex64(uint64_t v);

static isr_handler_t g_handlers[ISR_MAX_HANDLERS];

extern void* isr_stub_table[];   /* isr_stubs.asm: 48 stubs (32 exc + 16 IRQ) */

/* IDT (256 × 16-byte gates). */
struct __attribute__((packed)) idt_entry {
    uint16_t off_lo;
    uint16_t sel;
    uint8_t  ist;
    uint8_t  type;
    uint16_t off_mid;
    uint32_t off_hi;
    uint32_t reserved;
};
struct __attribute__((packed)) idt_ptr { uint16_t limit; uint64_t base; };

static struct idt_entry g_idt[256];
extern void idt64_load(struct idt_ptr* p);   /* idt_load.asm */

static void set_gate(int n, void* h, uint8_t type) {
    uint64_t a = (uint64_t)h;
    g_idt[n].off_lo  = a & 0xFFFF;
    g_idt[n].sel     = 0x08;
    g_idt[n].ist     = 0;
    g_idt[n].type    = type;
    g_idt[n].off_mid = (a >> 16) & 0xFFFF;
    g_idt[n].off_hi  = (a >> 32) & 0xFFFFFFFF;
    g_idt[n].reserved = 0;
}

static const char* const _names[32] = {
    "Divide Error","Debug","NMI","Breakpoint","Overflow","Bound Range",
    "Invalid Opcode","Device Not Available","Double Fault","Coproc Segment",
    "Invalid TSS","Segment Not Present","Stack Fault","General Protection",
    "Page Fault","Reserved","x87 FP","Alignment Check","Machine Check",
    "SIMD FP","Virtualization","Control Protection","Reserved","Reserved",
    "Reserved","Reserved","Reserved","Reserved","Hypervisor","VMM Comm",
    "Security","Reserved"
};

void isr_init(void) {
    for (int i = 0; i < ISR_MAX_HANDLERS; i++) g_handlers[i] = (isr_handler_t)0;
    for (int i = 0; i < 256; i++) set_gate(i, (void*)0, 0);
    /* 32 CPU exceptions + 16 IRQ stubs (vectors 0..47). */
    for (int i = 0; i < 48; i++) set_gate(i, isr_stub_table[i], 0x8E);

    struct idt_ptr p = { sizeof(g_idt) - 1, (uint64_t)g_idt };
    idt64_load(&p);
}

os_status_t isr_register_handler(uint8_t vector, isr_handler_t handler) {
    if (!handler) return OS_ERR_NULL;
    g_handlers[vector] = handler;
    return OS_OK;
}

void isr_handler(isr_frame_t* frame) {
    if (!frame) return;
    uint64_t vec = frame->vector;

    /* Hardware IRQs (32..47): dispatch then acknowledge the PIC. */
    if (vec >= 32 && vec < 48) {
        if (g_handlers[vec]) g_handlers[vec](frame);
        pic_send_eoi((uint8_t)(vec - 32));
        return;
    }

    if (vec < ISR_MAX_HANDLERS && g_handlers[vec]) {
        g_handlers[vec](frame);
        return;
    }

    if (vec < 32) {
        serial_write((const uint8_t*)"\n[noxis64] EXCEPTION ");
        serial_write_hex64(vec);
        serial_write((const uint8_t*)"  ");
        serial_write((const uint8_t*)_names[vec]);
        serial_write((const uint8_t*)"  err="); serial_write_hex64(frame->error_code);
        serial_write((const uint8_t*)"\n[noxis64] rip="); serial_write_hex64(frame->rip);
        uint64_t cr2; __asm__ __volatile__("mov %%cr2,%0" : "=r"(cr2));
        serial_write((const uint8_t*)" cr2="); serial_write_hex64(cr2);
        serial_write((const uint8_t*)"\n[noxis64] halted.\n");
        for (;;) __asm__ __volatile__("cli; hlt");
    }
    /* Unhandled IRQ — ignore for now. */
}
