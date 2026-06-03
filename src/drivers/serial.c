/**
 * @file    drivers/serial.c
 * @brief   16550 UART (COM1) — polling TX for kernel debug output (x86-64).
 */
#include <drivers/serial.h>
#include <kernel/hal/ports.h>
#include <common/types.h>

#define COM1 0x3F8

os_status_t serial_init(void) {
    port_byte_out(COM1 + 1, 0x00);   /* disable interrupts        */
    port_byte_out(COM1 + 3, 0x80);   /* enable DLAB               */
    port_byte_out(COM1 + 0, 0x03);   /* divisor low (38400 baud)  */
    port_byte_out(COM1 + 1, 0x00);   /* divisor high              */
    port_byte_out(COM1 + 3, 0x03);   /* 8N1                       */
    port_byte_out(COM1 + 2, 0xC7);   /* enable + clear FIFO       */
    port_byte_out(COM1 + 4, 0x0B);   /* IRQs off, RTS/DSR set     */
    return OS_OK;
}

void serial_putc(uint8_t c) {
    if (c == '\n') serial_putc('\r');
    while (!(port_byte_in(COM1 + 5) & 0x20)) { }
    port_byte_out(COM1, c);
}

void serial_write(const uint8_t* s) {
    for (; *s; s++) serial_putc(*s);
}

void serial_write_n(const uint8_t* buf, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) serial_putc(buf[i]);
}

void serial_write_hex(uint32_t v) {
    static const char hx[] = "0123456789ABCDEF";
    serial_putc('0'); serial_putc('x');
    for (int i = 28; i >= 0; i -= 4) serial_putc(hx[(v >> i) & 0xF]);
}

void serial_write_hex64(uint64_t v) {
    static const char hx[] = "0123456789ABCDEF";
    serial_putc('0'); serial_putc('x');
    for (int i = 60; i >= 0; i -= 4) serial_putc(hx[(v >> i) & 0xF]);
}
