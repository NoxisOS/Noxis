/**
 * @file    hal/idt.h
 * @brief   Interrupt Descriptor Table definitions
 * @author  Noxis Team
 * @date    2026-05-29
 */
#ifndef HAL_IDT_H
#define HAL_IDT_H

#include <common/types.h>
#include <common/status.h>

/* ── constants ─────────────────────────────────────────────── */
#define IDT_ENTRIES      256
#define IDT_GATE_TASK    0x5
#define IDT_GATE_INT16   0x6
#define IDT_GATE_TRAP16  0x7
#define IDT_GATE_INT32   0xE
#define IDT_GATE_TRAP32  0xF
#define IDT_DPL0         0x00
#define IDT_DPL3         0x60
#define IDT_PRESENT      0x80

/* ── types ─────────────────────────────────────────────────── */

/**
 * @brief A single IDT gate descriptor (8 bytes, hardware format)
 */
typedef struct __attribute__((packed)) {
    uint16_t offset_low;
    uint16_t selector;     /* CS segment selector */
    uint8_t  zero;         /* always 0 */
    uint8_t  flags;        /* P(1) DPL(2) 0(1) GateType(4) */
    uint16_t offset_high;
} idt_entry_t;

/**
 * @brief IDTR structure passed to lidt
 */
typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint32_t base;
} idt_ptr_t;

/* ── public functions ──────────────────────────────────────── */

/**
 * @brief Initializes the IDT with all 256 entries
 * @return OS_OK on success
 */
os_status_t idt_init(void);

/**
 * @brief Sets a single IDT gate entry
 * @param vector   Interrupt vector number (0-255)
 * @param handler  Base address of the ISR stub
 * @param flags    Gate type and DPL (e.g., IDT_PRESENT | IDT_DPL0 | IDT_GATE_INT32)
 * @return OS_OK on success, OS_ERR_INVALID if vector > 255
 */
os_status_t idt_set_gate(uint8_t vector, uint32_t handler, uint8_t flags);

#endif /* HAL_IDT_H */
