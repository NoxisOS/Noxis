//! x86 I/O port access via `in`/`out` instructions.

/// Generic typed I/O port.
pub struct Port<T: PortValue>(u16, core::marker::PhantomData<T>);

pub trait PortValue: Copy {
    unsafe fn read(port: u16) -> Self;
    unsafe fn write(port: u16, val: Self);
}

impl PortValue for u8 {
    unsafe fn read(port: u16) -> u8 {
        let v: u8;
        core::arch::asm!("in al, dx", out("al") v, in("dx") port, options(nomem, nostack));
        v
    }
    unsafe fn write(port: u16, val: u8) {
        core::arch::asm!("out dx, al", in("dx") port, in("al") val, options(nomem, nostack));
    }
}

impl PortValue for u16 {
    unsafe fn read(port: u16) -> u16 {
        let v: u16;
        core::arch::asm!("in ax, dx", out("ax") v, in("dx") port, options(nomem, nostack));
        v
    }
    unsafe fn write(port: u16, val: u16) {
        core::arch::asm!("out dx, ax", in("dx") port, in("ax") val, options(nomem, nostack));
    }
}

impl PortValue for u32 {
    unsafe fn read(port: u16) -> u32 {
        let v: u32;
        core::arch::asm!("in eax, dx", out("eax") v, in("dx") port, options(nomem, nostack));
        v
    }
    unsafe fn write(port: u16, val: u32) {
        core::arch::asm!("out dx, eax", in("dx") port, in("eax") val, options(nomem, nostack));
    }
}

impl<T: PortValue> Port<T> {
    pub const fn new(addr: u16) -> Self { Port(addr, core::marker::PhantomData) }
    /// # Safety: direct hardware access.
    pub unsafe fn read(&self) -> T  { T::read(self.0) }
    /// # Safety: direct hardware access.
    pub unsafe fn write(&self, v: T) { T::write(self.0, v) }
}

/// Short I/O delay (write to port 0x80, the POST "null" port).
#[inline]
pub fn io_wait() {
    unsafe { Port::<u8>::new(0x80).write(0) };
}
