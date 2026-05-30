/**
 * @file    syscall/syscall.c
 * @brief   System call dispatcher (int 0x80 + sysenter)
 * @author  Noxis Team
 * @date    2026-05-30
 */
#include <syscall/syscall.h>
#include <kernel/isr.h>
#include <hal/idt.h>
#include <proc/scheduler.h>
#include <proc/process.h>
#include <common/types.h>

#define MSR_SYSENTER_CS   0x174
#define MSR_SYSENTER_ESP  0x175
#define MSR_SYSENTER_EIP  0x176
#define VGA_BUFFER  ((volatile uint16_t*)0xB8000)
#define VGA_WIDTH   80

static uint32_t g_sys_row, g_sys_col;

extern void isr_stub_128(void);
extern void sysenter_entry(void);
extern void msr_write(uint32_t msr, uint32_t low, uint32_t high);

static void _sys_write(isr_frame_t* frame) {
    const uint8_t* str = (const uint8_t*)frame->ebx;
    uint32_t len = frame->esi;
    if (len == 0) len = frame->ecx;
    for (uint32_t i = 0; i < len && str[i]; i++) {
        if (str[i] == '\n') { g_sys_col = 0; g_sys_row++; }
        else {
            VGA_BUFFER[g_sys_row * VGA_WIDTH + g_sys_col] = (uint16_t)str[i] | 0x0F00;
            g_sys_col++;
            if (g_sys_col >= VGA_WIDTH) { g_sys_col = 0; g_sys_row++; }
        }
        if (g_sys_row >= 25) g_sys_row = 0;
    }
    frame->eax = len;
}

static void _sys_exit(isr_frame_t* frame) {
    (void)frame;
    for (;;);
}

static void _syscall_dispatch(isr_frame_t* frame) {
    switch (frame->eax) {
    case SYS_WRITE: _sys_write(frame); break;
    case SYS_EXIT:  _sys_exit(frame);  break;
    default: break;
    }
}

/* Called by both int 0x80 ISR and sysenter_entry */
void syscall_handler(isr_frame_t* frame) {
    _syscall_dispatch(frame);
}

os_status_t syscall_init(void) {
    isr_register_handler(128, syscall_handler);
    idt_set_gate(128, (uint32_t)isr_stub_128, IDT_PRESENT | IDT_DPL3 | IDT_GATE_INT32);
    /* sysenter loads ESP from this MSR; point it at a valid kernel stack TOP.
       Reuse idle's kstack since idle never actually runs in this demo flow. */
    msr_write(MSR_SYSENTER_CS,  0x08, 0);
    msr_write(MSR_SYSENTER_ESP, scheduler_current()->kstack_top, 0);
    msr_write(MSR_SYSENTER_EIP, (uint32_t)sysenter_entry, 0);
    return OS_OK;
}
