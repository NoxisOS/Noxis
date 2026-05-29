/**
 * @file    drivers/serial.h
 * @brief   Serial port (COM1) driver for debug output
 * @author  Noxis Team
 * @date    2026-05-29
 */
#ifndef DRIVERS_SERIAL_H
#define DRIVERS_SERIAL_H

#include <common/types.h>
#include <common/status.h>

/**
 * @brief Initializes COM1 serial port (38400 8N1)
 * @return OS_OK on success
 */
os_status_t serial_init(void);

/**
 * @brief Writes a single character to COM1
 * @param c  Character to write
 */
void serial_putc(uint8_t c);

/**
 * @brief Writes a null-terminated string to COM1
 * @param str  String to write
 */
void serial_write(const uint8_t* str);

#endif /* DRIVERS_SERIAL_H */
