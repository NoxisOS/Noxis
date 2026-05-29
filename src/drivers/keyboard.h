/**
 * @file    drivers/keyboard.h
 * @brief   PS/2 keyboard driver — scancode reading and buffered input
 * @author  Noxis Team
 * @date    2026-05-29
 */
#ifndef DRIVERS_KEYBOARD_H
#define DRIVERS_KEYBOARD_H

#include <common/types.h>
#include <common/status.h>

/* ── public functions ──────────────────────────────────────── */

/**
 * @brief Initializes the keyboard driver and registers the IRQ1 handler
 * @return OS_OK on success
 */
os_status_t kbd_init(void);

/**
 * @brief Reads the next character from the keyboard buffer (blocking)
 * @param out  Output: the ASCII character read
 * @return OS_OK on success
 */
os_status_t kbd_read(uint8_t* out);

/**
 * @brief Returns TRUE if characters are available in the buffer
 * @return TRUE if data is ready, FALSE otherwise
 */
bool_t kbd_has_data(void);

#endif /* DRIVERS_KEYBOARD_H */
