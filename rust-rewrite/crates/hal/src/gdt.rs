//! 64-bit GDT: null, kernel code, kernel data, user code, user data, TSS.

use x86_64::structures::gdt::{GlobalDescriptorTable, Descriptor, SegmentSelector};
use x86_64::structures::tss::TaskStateSegment;
use x86_64::VirtAddr;
use sync::Once;

/// IST index used for the double-fault handler stack (IST1 in the TSS).
pub const DOUBLE_FAULT_IST: u16 = 0;

struct GdtData {
    gdt:         GlobalDescriptorTable,
    kernel_code: SegmentSelector,
    kernel_data: SegmentSelector,
    user_code:   SegmentSelector,
    user_data:   SegmentSelector,
    tss_sel:     SegmentSelector,
}

static TSS: Once<TaskStateSegment> = Once::new();
static GDT: Once<GdtData>          = Once::new();

/// Statically allocated double-fault stack (8 KiB).
static mut DOUBLE_FAULT_STACK: [u8; 8192] = [0u8; 8192];

pub fn init() {
    let tss = TSS.call_once(|| {
        let mut tss = TaskStateSegment::new();
        tss.interrupt_stack_table[DOUBLE_FAULT_IST as usize] = VirtAddr::new(unsafe {
            DOUBLE_FAULT_STACK.as_ptr() as u64 + 8192
        });
        tss
    });

    let gdt_data = GDT.call_once(|| {
        let mut gdt = GlobalDescriptorTable::new();
        let kernel_code = gdt.append(Descriptor::kernel_code_segment());
        let kernel_data = gdt.append(Descriptor::kernel_data_segment());
        let user_data   = gdt.append(Descriptor::user_data_segment());
        let user_code   = gdt.append(Descriptor::user_code_segment());
        let tss_sel     = gdt.append(Descriptor::tss_segment(tss));
        GdtData { gdt, kernel_code, kernel_data, user_code, user_data, tss_sel }
    });

    gdt_data.gdt.load();

    use x86_64::instructions::segmentation::{CS, DS, SS, Segment};
    use x86_64::instructions::tables::load_tss;
    unsafe {
        CS::set_reg(gdt_data.kernel_code);
        DS::set_reg(gdt_data.kernel_data);
        SS::set_reg(gdt_data.kernel_data);
        load_tss(gdt_data.tss_sel);
    }
}

pub struct Selectors {
    pub kernel_code: SegmentSelector,
    pub kernel_data: SegmentSelector,
    pub user_code:   SegmentSelector,
    pub user_data:   SegmentSelector,
}

pub fn selectors() -> Selectors {
    let d = GDT.get().expect("GDT not initialized");
    Selectors {
        kernel_code: d.kernel_code,
        kernel_data: d.kernel_data,
        user_code:   d.user_code,
        user_data:   d.user_data,
    }
}
