/**
 * @file    syscall/syscall.c
 * @brief   System call dispatcher (int 0x80 + sysenter).
 *          Convention: EAX=#, EBX=arg1, ESI=arg2, ECX=arg3.
 * @author  Noxis Team
 * @date    2026-05-30
 */
#include <syscall/syscall.h>
#include <kernel/isr.h>
#include <hal/idt.h>
#include <proc/scheduler.h>
#include <proc/process.h>
#include <proc/exec.h>
#include <drivers/vga.h>
#include <common/types.h>

#define MSR_SYSENTER_CS   0x174
#define MSR_SYSENTER_ESP  0x175
#define MSR_SYSENTER_EIP  0x176

extern void isr_stub_128(void);
extern void sysenter_entry(void);
extern void msr_write(uint32_t msr, uint32_t low, uint32_t high);

static void _sys_write(isr_frame_t* frame) {
    const uint8_t* str = (const uint8_t*)frame->ebx;
    uint32_t len = frame->esi;
    if (len == 0) len = frame->ecx;

    /* Honor the user's length, but still stop at NUL — saves us from a runaway
       loop when the user passed a string-style buffer. */
    for (uint32_t i = 0; i < len; i++) {
        uint8_t c = str[i];
        if (c == 0) break;
        vga_put_char(c);
    }
    frame->eax = len;
}

static void _sys_exit(isr_frame_t* frame) {
    /* Hand the exit code back to whoever launched us. Never returns. */
    exec_return((int)frame->ebx);
}

static void _syscall_dispatch(isr_frame_t* frame) {
    switch (frame->eax) {
    case SYS_WRITE: _sys_write(frame); break;
    case SYS_EXIT:  _sys_exit(frame);  break;
    default: break;
    }
}

void syscall_handler(isr_frame_t* frame) {
    _syscall_dispatch(frame);
}

os_status_t syscall_init(void) {
    isr_register_handler(128, syscall_handler);
    idt_set_gate(128, (uint32_t)isr_stub_128,
                 IDT_PRESENT | IDT_DPL3 | IDT_GATE_INT32);
    msr_write(MSR_SYSENTER_CS,  0x08, 0);
    msr_write(MSR_SYSENTER_ESP, scheduler_current()->kstack_top, 0);
    msr_write(MSR_SYSENTER_EIP, (uint32_t)sysenter_entry, 0);
    return OS_OK;
}
