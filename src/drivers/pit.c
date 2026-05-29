/**
 * @file    drivers/pit.c
 * @brief   PIT timer — IRQ0 handler, tick counting, sleep
 * @author  Noxis Team
 * @date    2026-05-29
 */
#include <drivers/pit.h>
#include <kernel/isr.h>
#include <hal/ports.h>
#include <hal/pic.h>
#include <proc/scheduler.h>
#include <common/types.h>

/* ── PIT port constants ─────────────────────────────────────── */
#define PIT_CHANNEL0  0x40
#define PIT_CMD       0x43

/* ── file-scope state ──────────────────────────────────────── */
static volatile uint32_t g_ticks;

/* ── ISR handler ───────────────────────────────────────────── */
static void _pit_isr(isr_frame_t* frame) {
    g_ticks++;
    scheduler_tick(frame);
}

/* ── public functions ──────────────────────────────────────── */

os_status_t pit_init(uint32_t hz) {
    if (hz == 0 || hz > PIT_BASE_FREQ) return OS_ERR_INVALID;

    uint32_t divisor = PIT_BASE_FREQ / hz;
    if (divisor > 65535) divisor = 65535;
    if (divisor < 1)    divisor = 1;

    g_ticks = 0;

    /* Register ISR for IRQ0 (vector 0x20) */
    os_status_t status = isr_register_handler(0x20, _pit_isr);
    if (status != OS_OK) return status;

    /* Set PIT to rate generator mode */
    port_byte_out(PIT_CMD, 0x36);          /* channel 0, lobyte/hibyte, rate gen, binary */
    port_byte_out(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    port_byte_out(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));

    /* Unmask IRQ0 */
    pic_unmask(0);

    return OS_OK;
}

void pit_sleep_ms(uint32_t ms) {
    uint32_t target = g_ticks + ms;
    while (g_ticks < target);
}

uint32_t pit_uptime_ms(void) {
    return g_ticks;
}
