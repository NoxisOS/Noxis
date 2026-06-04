//! 8259A PIC — remap IRQs to vectors 0x20–0x2F, selective masking.

use crate::port::{Port, io_wait};

const PIC1_CMD:  u16 = 0x20;
const PIC1_DATA: u16 = 0x21;
const PIC2_CMD:  u16 = 0xA0;
const PIC2_DATA: u16 = 0xA1;

const ICW1_INIT: u8 = 0x11;
const ICW4_8086: u8 = 0x01;
const PIC_EOI:   u8 = 0x20;

/// Base interrupt vector for IRQ0.
pub const IRQ_OFFSET: u8 = 0x20;

/// Remap both PICs and mask all IRQs except the essential ones.
pub fn init() {
    unsafe {
        // Save current masks
        let _mask1 = Port::<u8>::new(PIC1_DATA).read();
        let _mask2 = Port::<u8>::new(PIC2_DATA).read();

        // Initialize in cascade mode
        Port::<u8>::new(PIC1_CMD).write(ICW1_INIT);  io_wait();
        Port::<u8>::new(PIC2_CMD).write(ICW1_INIT);  io_wait();
        Port::<u8>::new(PIC1_DATA).write(IRQ_OFFSET);      io_wait(); // PIC1 → 0x20
        Port::<u8>::new(PIC2_DATA).write(IRQ_OFFSET + 8);  io_wait(); // PIC2 → 0x28
        Port::<u8>::new(PIC1_DATA).write(4); io_wait(); // PIC1: cascade on IRQ2
        Port::<u8>::new(PIC2_DATA).write(2); io_wait(); // PIC2: cascade identity
        Port::<u8>::new(PIC1_DATA).write(ICW4_8086); io_wait();
        Port::<u8>::new(PIC2_DATA).write(ICW4_8086); io_wait();

        // Unmask IRQ0 (timer), IRQ1 (kbd), IRQ2 (cascade); mask everything else
        Port::<u8>::new(PIC1_DATA).write(0b11111000);
        Port::<u8>::new(PIC2_DATA).write(0xFF);
    }
}

/// Send EOI to the PIC(s) corresponding to the given IRQ number.
#[inline]
pub fn eoi(irq: u8) {
    unsafe {
        if irq >= 8 {
            Port::<u8>::new(PIC2_CMD).write(PIC_EOI);
        }
        Port::<u8>::new(PIC1_CMD).write(PIC_EOI);
    }
}

/// Unmask (enable) an IRQ line.
pub fn unmask(irq: u8) {
    let (port, bit) = if irq < 8 { (PIC1_DATA, irq) } else { (PIC2_DATA, irq - 8) };
    unsafe {
        let v = Port::<u8>::new(port).read();
        Port::<u8>::new(port).write(v & !(1 << bit));
    }
}

/// Mask (disable) an IRQ line.
pub fn mask(irq: u8) {
    let (port, bit) = if irq < 8 { (PIC1_DATA, irq) } else { (PIC2_DATA, irq - 8) };
    unsafe {
        let v = Port::<u8>::new(port).read();
        Port::<u8>::new(port).write(v | (1 << bit));
    }
}
