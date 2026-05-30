/**
 * @file    drivers/serial.c
 * @brief   16550 UART driver (COM1) — polling transmit for kernel logging.
 *
 *  Output-only by design: interrupts are left disabled on the chip, and
 *  bytes are sent by polling the Line Status Register.  This gives the
 *  kernel a reliable debug channel that shows up on QEMU's `-serial stdio`,
 *  which is far easier to inspect than the VGA framebuffer.
 *
 * @author  Noxis Team
 * @date    2026-05-30
 */
#include <drivers/serial.h>
#include <kernel/hal/ports.h>
#include <common/types.h>

/* ── COM1 register offsets (DLAB=0 unless noted) ────────────── */
#define COM1            0x3F8
#define UART_DATA       (COM1 + 0)   /* RX/TX  (divisor low when DLAB=1) */
#define UART_IER        (COM1 + 1)   /* int enable (divisor high, DLAB=1) */
#define UART_FCR        (COM1 + 2)   /* FIFO control (write)             */
#define UART_LCR        (COM1 + 3)   /* line control                     */
#define UART_MCR        (COM1 + 4)   /* modem control                    */
#define UART_LSR        (COM1 + 5)   /* line status                      */

#define LSR_THRE        0x20         /* transmitter holding register empty */

static int _ready;

os_status_t serial_init(void) {
    _ready = 0;

    port_byte_out(UART_IER, 0x00);   /* disable all UART interrupts        */
    port_byte_out(UART_LCR, 0x80);   /* DLAB=1 to set the baud divisor     */
    port_byte_out(UART_DATA, 0x01);  /* divisor low  = 1 → 115200 baud      */
    port_byte_out(UART_IER, 0x00);   /* divisor high = 0                    */
    port_byte_out(UART_LCR, 0x03);   /* DLAB=0, 8 bits, no parity, 1 stop   */
    port_byte_out(UART_FCR, 0xC7);   /* enable+clear FIFO, 14-byte trigger  */
    port_byte_out(UART_MCR, 0x0B);   /* DTR, RTS, OUT2                      */

    /* Loopback self-test: echo a byte through the chip. */
    port_byte_out(UART_MCR, 0x1E);   /* loopback mode                       */
    port_byte_out(UART_DATA, 0xAE);
    if (port_byte_in(UART_DATA) != 0xAE) return OS_ERR_IO;

    /* Back to normal operation. */
    port_byte_out(UART_MCR, 0x0F);
    _ready = 1;
    return OS_OK;
}

static void _putc_raw(uint8_t c) {
    while (!(port_byte_in(UART_LSR) & LSR_THRE))
        ;
    port_byte_out(UART_DATA, c);
}

void serial_putc(uint8_t c) {
    if (!_ready) return;
    if (c == '\n') _putc_raw('\r');
    _putc_raw(c);
}

void serial_write(const uint8_t* s) {
    if (!_ready || !s) return;
    for (uint32_t i = 0; s[i]; i++) serial_putc(s[i]);
}

void serial_write_n(const uint8_t* buf, uint32_t len) {
    if (!_ready || !buf) return;
    for (uint32_t i = 0; i < len; i++) serial_putc(buf[i]);
}

void serial_write_hex(uint32_t v) {
    static const uint8_t hx[] = "0123456789ABCDEF";
    if (!_ready) return;
    serial_putc('0'); serial_putc('x');
    for (int32_t i = 28; i >= 0; i -= 4)
        serial_putc(hx[(v >> i) & 0xF]);
}
