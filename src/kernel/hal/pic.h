/**
 * @file    hal/pic.h
 * @brief   8259A PIC remapping and control
 * @author  Noxis Team
 * @date    2026-05-29
 */
#ifndef HAL_PIC_H
#define HAL_PIC_H

#include <common/types.h>
#include <common/status.h>

/* ── PIC port constants ────────────────────────────────────── */
#define PIC_MASTER_CMD   0x20
#define PIC_MASTER_DATA  0x21
#define PIC_SLAVE_CMD    0xA0
#define PIC_SLAVE_DATA   0xA1
#define PIC_EOI          0x20

/* ── Remapped vector bases ─────────────────────────────────── */
#define PIC_MASTER_VECTOR  0x20
#define PIC_SLAVE_VECTOR   0x28

/* ── public functions ──────────────────────────────────────── */

/**
 * @brief Remaps the PIC so IRQ0-7 → vectors 0x20-0x27
 *        and IRQ8-15 → vectors 0x28-0x2F.
 *        Must be called before enabling interrupts.
 * @return OS_OK on success
 */
os_status_t pic_remap(void);

/**
 * @brief Sends End-Of-Interrupt to the appropriate PIC(s)
 * @param irq  IRQ number (0-15)
 */
void pic_send_eoi(uint8_t irq);

/**
 * @brief Masks (disables) a specific IRQ line
 * @param irq  IRQ number (0-15)
 */
void pic_mask(uint8_t irq);

/**
 * @brief Unmasks (enables) a specific IRQ line
 * @param irq  IRQ number (0-15)
 */
void pic_unmask(uint8_t irq);

#endif /* HAL_PIC_H */
