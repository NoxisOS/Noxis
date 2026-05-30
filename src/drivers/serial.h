/**
 * @file    drivers/serial.h
 * @brief   16550 UART driver (COM1) — polling TX for kernel debug output.
 * @author  Noxis Team
 * @date    2026-05-30
 */
#ifndef DRIVERS_SERIAL_H
#define DRIVERS_SERIAL_H

#include <common/types.h>
#include <common/status.h>

/**
 * @brief Initialise COM1 at 115200 baud, 8N1.
 * @return OS_OK on success, OS_ERR_IO if the UART fails its loopback test.
 */
os_status_t serial_init(void);

/** @brief Write one byte to COM1 (translates '\n' → "\r\n"). */
void serial_putc(uint8_t c);

/** @brief Write a null-terminated string to COM1. */
void serial_write(const uint8_t* s);

/** @brief Write `len` raw bytes to COM1. */
void serial_write_n(const uint8_t* buf, uint32_t len);

/** @brief Write a 32-bit value as 0x-prefixed hex (debug helper). */
void serial_write_hex(uint32_t v);

#endif /* DRIVERS_SERIAL_H */
