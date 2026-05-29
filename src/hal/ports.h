/**
 * @file    hal/ports.h
 * @brief   x86 port I/O function declarations
 * @author  Noxis Team
 * @date    2026-05-29
 */
#ifndef HAL_PORTS_H
#define HAL_PORTS_H

#include <common/types.h>

/* ── public functions ──────────────────────────────────────── */

uint8_t  port_byte_in(uint16_t port);
void     port_byte_out(uint16_t port, uint8_t data);
uint16_t port_word_in(uint16_t port);
void     port_word_out(uint16_t port, uint16_t data);
uint32_t port_dword_in(uint16_t port);
void     port_dword_out(uint16_t port, uint32_t data);
void     io_delay(void);

#endif /* HAL_PORTS_H */
