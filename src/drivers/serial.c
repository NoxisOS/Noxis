/**
 * @file    drivers/serial.c
 * @brief   Serial port (COM1) driver — 38400 baud, 8N1
 * @author  Noxis Team
 * @date    2026-05-29
 */
#include <drivers/serial.h>
#include <hal/ports.h>
#include <common/types.h>

/* ── COM1 port constants ───────────────────────────────────── */
#define COM1_BASE       0x3F8
#define COM1_DATA       (COM1_BASE + 0)   /* DLAB=0: R/W data */
#define COM1_IER        (COM1_BASE + 1)   /* DLAB=0: interrupt enable */
#define COM1_DLL        (COM1_BASE + 0)   /* DLAB=1: divisor low */
#define COM1_DLH        (COM1_BASE + 1)   /* DLAB=1: divisor high */
#define COM1_FCR        (COM1_BASE + 2)   /* FIFO control */
#define COM1_LCR        (COM1_BASE + 3)   /* line control */
#define COM1_MCR        (COM1_BASE + 4)   /* modem control */
#define COM1_LSR        (COM1_BASE + 5)   /* line status */
#define COM1_LSR_TXRDY  0x20              /* transmit holding reg empty */

/* ── public functions ──────────────────────────────────────── */

os_status_t serial_init(void) {
    /* Disable interrupts */
    port_byte_out(COM1_IER, 0x00);

    /* Set DLAB to configure divisor */
    port_byte_out(COM1_LCR, 0x80);

    /* Set divisor to 3 (115200 / 3 = 38400 baud) */
    port_byte_out(COM1_DLL, 0x03);
    port_byte_out(COM1_DLH, 0x00);

    /* 8N1, clear DLAB */
    port_byte_out(COM1_LCR, 0x03);

    /* Enable FIFO, clear, 14-byte threshold */
    port_byte_out(COM1_FCR, 0xC7);

    /* RTS + DTR + aux output 2 (for QEMU) */
    port_byte_out(COM1_MCR, 0x0B);

    return OS_OK;
}

void serial_putc(uint8_t c) {
    /* Wait for transmit buffer to be empty */
    while (!(port_byte_in(COM1_LSR) & COM1_LSR_TXRDY));
    port_byte_out(COM1_DATA, c);

    /* Carriage return after newline for proper terminal display */
    if (c == '\n') {
        while (!(port_byte_in(COM1_LSR) & COM1_LSR_TXRDY));
        port_byte_out(COM1_DATA, '\r');
    }
}

void serial_write(const uint8_t* str) {
    for (uint32_t i = 0; str[i] != '\0'; i++) {
        serial_putc(str[i]);
    }
}
