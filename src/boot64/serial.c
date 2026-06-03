/**
 * @file    src/boot64/serial.c
 * @brief   16550 UART (COM1) — debug output for the 64-bit kernel.
 */
#include "types.h"

#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, 0x00);   /* disable interrupts          */
    outb(COM1 + 3, 0x80);   /* enable DLAB                 */
    outb(COM1 + 0, 0x03);   /* divisor low  (38400 baud)   */
    outb(COM1 + 1, 0x00);   /* divisor high                */
    outb(COM1 + 3, 0x03);   /* 8 bits, no parity, 1 stop   */
    outb(COM1 + 2, 0xC7);   /* enable+clear FIFO, 14-byte  */
    outb(COM1 + 4, 0x0B);   /* IRQs off, RTS/DSR set       */
}

static void serial_putc(char c) {
    if (c == '\n') serial_putc('\r');
    while (!(inb(COM1 + 5) & 0x20)) { }   /* wait for THR empty */
    outb(COM1, (uint8_t)c);
}

void serial_write(const char* s) {
    for (; *s; s++) serial_putc(*s);
}

void serial_hex(uint64_t v) {
    static const char hx[] = "0123456789ABCDEF";
    serial_write("0x");
    for (int i = 60; i >= 0; i -= 4)
        serial_putc(hx[(v >> i) & 0xF]);
}
