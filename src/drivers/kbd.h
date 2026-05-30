/**
 * @file    drivers/kbd.h
 * @brief   PS/2 keyboard driver — IRQ1, scancode set 1
 * @author  Noxis Team
 * @date    2026-05-30
 */
#ifndef DRIVERS_KBD_H
#define DRIVERS_KBD_H

#include <common/types.h>
#include <common/status.h>

/**
 * @brief Register IRQ1 handler, drain pending bytes, unmask IRQ.
 */
os_status_t kbd_init(void);

/**
 * @brief Non-blocking read. Returns -1 if no char is available,
 *        otherwise the ASCII byte (0..255).
 */
int32_t kbd_poll(void);

/**
 * @brief Blocking read — yields to the scheduler until a key arrives.
 *        The keyboard ISR wakes the calling thread via scheduler_wake,
 *        allowing other threads to run while waiting.  0% CPU.
 */
uint8_t kbd_getchar(void);

#endif /* DRIVERS_KBD_H */
