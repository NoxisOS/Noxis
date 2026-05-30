/**
 * @file    hal/pic.c
 * @brief   8259A Programmable Interrupt Controller remap and EOI
 * @author  Noxis Team
 * @date    2026-05-29
 */
#include <hal/pic.h>
#include <hal/ports.h>
#include <common/types.h>

/* ── public functions ──────────────────────────────────────── */

os_status_t pic_remap(void) {
    /* ICW1: Start initialization in cascade mode */
    port_byte_out(PIC_MASTER_CMD, 0x11);
    io_delay();
    port_byte_out(PIC_SLAVE_CMD, 0x11);
    io_delay();

    /* ICW2: Base vector offsets */
    port_byte_out(PIC_MASTER_DATA, PIC_MASTER_VECTOR);
    io_delay();
    port_byte_out(PIC_SLAVE_DATA, PIC_SLAVE_VECTOR);
    io_delay();

    /* ICW3: Cascade wiring */
    port_byte_out(PIC_MASTER_DATA, 0x04);  /* slave on IRQ2 */
    io_delay();
    port_byte_out(PIC_SLAVE_DATA, 0x02);   /* cascade identity */
    io_delay();

    /* ICW4: 8086/88 mode */
    port_byte_out(PIC_MASTER_DATA, 0x01);
    io_delay();
    port_byte_out(PIC_SLAVE_DATA, 0x01);
    io_delay();

    /* Mask all interrupts — drivers unmask individually */
    port_byte_out(PIC_MASTER_DATA, 0xFF);
    port_byte_out(PIC_SLAVE_DATA, 0xFF);

    return OS_OK;
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        port_byte_out(PIC_SLAVE_CMD, PIC_EOI);
    }
    port_byte_out(PIC_MASTER_CMD, PIC_EOI);
}

void pic_mask(uint8_t irq) {
    uint16_t port;
    uint8_t  value;

    if (irq < 8) {
        port = PIC_MASTER_DATA;
    } else {
        port = PIC_SLAVE_DATA;
        irq -= 8;
    }

    value = port_byte_in(port) | (uint8_t)(1 << irq);
    port_byte_out(port, value);
}

void pic_unmask(uint8_t irq) {
    uint16_t port;
    uint8_t  value;

    if (irq < 8) {
        port = PIC_MASTER_DATA;
    } else {
        port = PIC_SLAVE_DATA;
        irq -= 8;
    }

    value = port_byte_in(port) & (uint8_t)(~(1 << irq));
    port_byte_out(port, value);
}
