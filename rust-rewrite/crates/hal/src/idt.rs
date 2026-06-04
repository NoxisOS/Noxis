//! IDT — x86-64 interrupt descriptor table.
//!
//! Handled vectors:
//!   0x00–0x1F : CPU exceptions (double-fault uses IST stack)
//!   0x20      : IRQ0 / PIT timer
//!   0x21      : IRQ1 / keyboard
//!   0x80      : syscall trap gate (stub — replaced by SYSCALL/SYSRET later)

use x86_64::structures::idt::{InterruptDescriptorTable, InterruptStackFrame, PageFaultErrorCode};
use sync::Once;
use crate::pic;

// The IDT must remain at a fixed address for the lifetime of the kernel.
static mut IDT: InterruptDescriptorTable = InterruptDescriptorTable::new();

pub fn init() {
    unsafe {
        // ── Exceptions ─────────────────────────────────────────────────────
        IDT.breakpoint.set_handler_fn(breakpoint_handler);
        IDT.double_fault
            .set_handler_fn(double_fault_handler)
            .set_stack_index(crate::gdt::DOUBLE_FAULT_IST);
        IDT.page_fault.set_handler_fn(page_fault_handler);
        IDT.general_protection_fault.set_handler_fn(gpf_handler);
        IDT.invalid_opcode.set_handler_fn(invalid_opcode_handler);
        IDT.stack_segment_fault.set_handler_fn(stack_fault_handler);
        IDT.segment_not_present.set_handler_fn(snp_handler);

        // ── Hardware IRQs (user-defined entries, indexed by u8) ────────────
        IDT[(pic::IRQ_OFFSET) as u8].set_handler_fn(irq0_timer);
        IDT[(pic::IRQ_OFFSET + 1) as u8].set_handler_fn(irq1_kbd);

        // ── Syscall trap INT 0x80 ──────────────────────────────────────────
        IDT[0x80u8].set_handler_fn(syscall_trap);

        IDT.load();
    }
}

// ── Handlers ──────────────────────────────────────────────────────────────────

extern "x86-interrupt" fn breakpoint_handler(_frame: InterruptStackFrame) {}

extern "x86-interrupt" fn double_fault_handler(
    frame: InterruptStackFrame, _code: u64
) -> ! {
    panic!("DOUBLE FAULT @ {:#x}", frame.instruction_pointer.as_u64());
}

extern "x86-interrupt" fn page_fault_handler(
    frame: InterruptStackFrame,
    code: PageFaultErrorCode,
) {
    use x86_64::registers::control::Cr2;
    let addr = Cr2::read().unwrap_or(x86_64::VirtAddr::zero()).as_u64();
    // Dispatch to mm::vmm via a registered hook (avoids circular deps)
    if let Some(h) = PAGE_FAULT_HOOK.get() {
        if h(addr, code.bits()) { return; }
    }
    panic!(
        "PAGE FAULT addr={:#x} code={:#x} ip={:#x}",
        addr, code.bits(), frame.instruction_pointer.as_u64()
    );
}

extern "x86-interrupt" fn gpf_handler(frame: InterruptStackFrame, code: u64) {
    panic!("GPF code={:#x} ip={:#x}", code, frame.instruction_pointer.as_u64());
}

extern "x86-interrupt" fn invalid_opcode_handler(frame: InterruptStackFrame) {
    panic!("INVALID OPCODE @ {:#x}", frame.instruction_pointer.as_u64());
}

extern "x86-interrupt" fn stack_fault_handler(frame: InterruptStackFrame, code: u64) {
    panic!("STACK FAULT code={:#x} @ {:#x}", code, frame.instruction_pointer.as_u64());
}

extern "x86-interrupt" fn snp_handler(frame: InterruptStackFrame, code: u64) {
    panic!("SEGMENT NOT PRESENT code={:#x} @ {:#x}", code, frame.instruction_pointer.as_u64());
}

extern "x86-interrupt" fn irq0_timer(_frame: InterruptStackFrame) {
    if let Some(h) = TIMER_HOOK.get() { h(); }
    pic::eoi(0);
}

extern "x86-interrupt" fn irq1_kbd(_frame: InterruptStackFrame) {
    if let Some(h) = KBD_HOOK.get() { h(); }
    pic::eoi(1);
}

extern "x86-interrupt" fn syscall_trap(_frame: InterruptStackFrame) {
    // INT 0x80 stub — will be replaced by SYSCALL/SYSRET in tier P8
}

// ── Hooks — registered by mm/sched/drivers without circular dependencies ──────

pub type PageFaultFn = fn(addr: u64, code: u64) -> bool;
pub type IrqFn      = fn();

static PAGE_FAULT_HOOK: Once<PageFaultFn> = Once::new();
static TIMER_HOOK:      Once<IrqFn>       = Once::new();
static KBD_HOOK:        Once<IrqFn>       = Once::new();

pub fn register_page_fault(f: PageFaultFn) { PAGE_FAULT_HOOK.call_once(|| f); }
pub fn register_timer(f: IrqFn)            { TIMER_HOOK.call_once(|| f); }
pub fn register_kbd(f: IrqFn)              { KBD_HOOK.call_once(|| f); }
