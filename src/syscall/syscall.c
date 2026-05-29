/**
 * @file    syscall/syscall.c
 * @brief   System call dispatcher (int 0x80)
 * @author  Noxis Team
 * @date    2026-05-30
 */
#include <syscall/syscall.h>
#include <kernel/isr.h>
#include <hal/idt.h>
#include <common/types.h>

/* ── VGA for sys_write ─────────────────────────────────────── */
#define VGA_BUFFER  ((volatile uint16_t*)0xB8000)
#define VGA_WIDTH   80
static uint32_t g_sys_row = 0;
static uint32_t g_sys_col = 0;

/* ── external ASM stub for vector 0x80 ─────────────────────── */
extern void isr_stub_128(void);

/* ── syscall handler prototypes ────────────────────────────── */
static void _sys_write(isr_frame_t* frame);
static void _sys_exit(isr_frame_t* frame);

/* ── private functions ─────────────────────────────────────── */

static void _syscall_dispatch(isr_frame_t* frame) {
    switch (frame->eax) {
    case SYS_WRITE:
        _sys_write(frame);
        break;
    case SYS_EXIT:
        _sys_exit(frame);
        break;
    default:
        break;
    }
}

static void _sys_write(isr_frame_t* frame) {
    const uint8_t* str = (const uint8_t*)frame->ebx;
    uint32_t len       = frame->ecx;
    for (uint32_t i = 0; i < len && str[i]; i++) {
        if (str[i] == '\n') { g_sys_col = 0; g_sys_row++; }
        else {
            VGA_BUFFER[g_sys_row * VGA_WIDTH + g_sys_col] =
                (uint16_t)str[i] | 0x0F00;
            g_sys_col++;
            if (g_sys_col >= VGA_WIDTH) { g_sys_col = 0; g_sys_row++; }
        }
        if (g_sys_row >= 25) g_sys_row = 0;
    }
    frame->eax = len;
}

static void _sys_exit(isr_frame_t* frame) {
    (void)frame;
    /* Halt the process — for now just loop */
    for (;;);
}

/* ── public functions ──────────────────────────────────────── */

os_status_t syscall_init(void) {
    /* Register int 0x80 stub — give it a temporary IDT gate */
    /* The stub jumps through isr_common which saves context */

    /* Register the dispatcher as ISR handler for vector 128 */
    isr_register_handler(128, _syscall_dispatch);

    /* Set IDT gate 0x80 with DPL3 (ring 3 can call it) */
    idt_set_gate(128, (uint32_t)isr_stub_128,
                 IDT_PRESENT | IDT_DPL3 | IDT_GATE_INT32);

    return OS_OK;
}
