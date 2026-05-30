/**
 * @file    kernel/isr.h
 * @brief   Interrupt Service Routine dispatcher and handler registration
 * @author  Noxis Team
 * @date    2026-05-29
 */
#ifndef KERNEL_ISR_H
#define KERNEL_ISR_H

#include <common/types.h>
#include <common/status.h>

/* ── constants ─────────────────────────────────────────────── */
#define ISR_MAX_HANDLERS  256

/* ── types ─────────────────────────────────────────────────── */

/**
 * @brief Stack frame passed to every ISR handler.
 *        Matches the exact order of pushes in isr_common:
 *          1. pushad → EAX first (at lowest addr), EDI last
 *          2. push ds, es, fs, gs (gs last = at ESP)
 *          3. error_code, vector (pushed by stub)
 *          4. eip, cs, eflags (pushed by CPU)
 *          5. user_esp, user_ss (if ring change)
 */
typedef struct __attribute__((packed)) {
    /* pushed by isr_common: gs last → at ESP (lowest addr) */
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;
    /* pushad pushes EAX first (highest addr in group), EDI last (lowest).
       Struct fields go from lowest to highest, so EDI first. */
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp_val;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    /* stub pushes error_code first then vector → vector at lower addr */
    uint32_t vector;
    uint32_t error_code;
    /* pushed by CPU on interrupt */
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    /* pushed by CPU on ring change (ring3→ring0 only) */
    uint32_t user_esp;
    uint32_t user_ss;
} isr_frame_t;

/**
 * @brief ISR handler function pointer type
 * @param frame  Pointer to the interrupt stack frame
 */
typedef void (*isr_handler_t)(isr_frame_t* frame);

/* ── public functions ──────────────────────────────────────── */

/**
 * @brief Initializes the ISR subsystem. Registers default
 *        handlers for all CPU exceptions (panic on fault).
 */
void isr_init(void);

/**
 * @brief Registers a custom handler for an interrupt vector
 * @param vector   Interrupt vector number (0-255)
 * @param handler  Handler function
 * @return OS_OK on success, OS_ERR_INVALID if vector >= 256
 */
os_status_t isr_register_handler(uint8_t vector, isr_handler_t handler);

/**
 * @brief The C-side ISR dispatcher called from isr_common.
 *        Do NOT call directly — only invoked via ASM stub.
 * @param frame  Pointer to the interrupt stack frame
 */
void isr_handler(isr_frame_t* frame);

#endif /* KERNEL_ISR_H */
