/**
 * @file    kernel/isr.c
 * @brief   ISR dispatcher — routes interrupts to registered handlers
 * @author  Noxis Team
 * @date    2026-05-29
 */
#include <kernel/isr/isr.h>
#include <kernel/core/panic.h>
#include <kernel/hal/pic.h>
#include <common/types.h>

/* ── file-scope state ──────────────────────────────────────── */
static isr_handler_t g_handlers[ISR_MAX_HANDLERS];

/* ── public functions ──────────────────────────────────────── */

void isr_init(void) {
    /* Zero all handlers */
    for (uint32_t i = 0; i < ISR_MAX_HANDLERS; i++) {
        g_handlers[i] = (isr_handler_t)0;
    }
}

os_status_t isr_register_handler(uint8_t vector, isr_handler_t handler) {
    if (!handler) return OS_ERR_NULL;
    g_handlers[vector] = handler;
    return OS_OK;
}

/*
 * Exception name table for panic messages
 */
static const uint8_t* _exception_names[] = {
    (const uint8_t*)"Divide Error",
    (const uint8_t*)"Debug",
    (const uint8_t*)"NMI",
    (const uint8_t*)"Breakpoint",
    (const uint8_t*)"Overflow",
    (const uint8_t*)"Bound Range",
    (const uint8_t*)"Invalid Opcode",
    (const uint8_t*)"Device Not Available",
    (const uint8_t*)"Double Fault",
    (const uint8_t*)"Coprocessor Segment",
    (const uint8_t*)"Invalid TSS",
    (const uint8_t*)"Segment Not Present",
    (const uint8_t*)"Stack Fault",
    (const uint8_t*)"General Protection",
    (const uint8_t*)"Page Fault",
    (const uint8_t*)"Reserved",
    (const uint8_t*)"x87 FP",
    (const uint8_t*)"Alignment Check",
    (const uint8_t*)"Machine Check",
    (const uint8_t*)"SIMD FP",
    (const uint8_t*)"Virtualization",
    (const uint8_t*)"Control Protection",
    (const uint8_t*)"Reserved",
    (const uint8_t*)"Reserved",
    (const uint8_t*)"Reserved",
    (const uint8_t*)"Reserved",
    (const uint8_t*)"Reserved",
    (const uint8_t*)"Reserved",
    (const uint8_t*)"Hypervisor",
    (const uint8_t*)"VMM Comm",
    (const uint8_t*)"Security",
    (const uint8_t*)"Reserved",
};

void isr_handler(isr_frame_t* frame) {
    if (!frame) return;

    /* Handle IRQs: send EOI before dispatching */
    if (frame->vector >= 0x20 && frame->vector < 0x30) {
        pic_send_eoi((uint8_t)(frame->vector - 0x20));
    }

    /* Dispatch to registered handler if any */
    if (g_handlers[frame->vector]) {
        g_handlers[frame->vector](frame);
        return;
    }

    /* CPU exceptions (0-31): panic with info */
    if (frame->vector < 32) {
        const uint8_t* name = _exception_names[frame->vector];
        kernel_panic(name, frame);
    }

    /* Unhandled interrupt — ignore silently */
}
