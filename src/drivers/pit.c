/**
 * @file    drivers/pit.c
 * @brief   PIT (8253/8254) timer — IRQ0 tick counting (x86-64).
 *
 * NOTE: scheduler_tick() will be re-wired here once the scheduler is
 * ported back in.  For now the handler just advances the tick counter.
 */
#include <drivers/pit.h>
#include <kernel/isr/isr.h>
#include <kernel/hal/ports.h>
#include <kernel/hal/pic.h>
#include <common/types.h>

#define PIT_CHANNEL0  0x40
#define PIT_CMD       0x43

static volatile uint32_t g_ticks;
static void (*g_tick_cb)(isr_frame_t*);   /* scheduler hook (optional) */

void pit_set_tick_cb(void (*cb)(isr_frame_t*)) { g_tick_cb = cb; }


static void _pit_isr(isr_frame_t* frame) {
    g_ticks++;
    if (g_tick_cb) g_tick_cb(frame);
}

os_status_t pit_init(uint32_t hz) {
    if (hz == 0 || hz > PIT_BASE_FREQ) return OS_ERR_INVALID;

    uint32_t divisor = PIT_BASE_FREQ / hz;
    if (divisor > 65535) divisor = 65535;
    if (divisor < 1)     divisor = 1;

    g_ticks = 0;

    os_status_t st = isr_register_handler(0x20, _pit_isr);   /* IRQ0 → vector 32 */
    if (st != OS_OK) return st;

    port_byte_out(PIT_CMD, 0x36);
    port_byte_out(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    port_byte_out(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));

    pic_unmask(0);
    return OS_OK;
}

void pit_sleep_ms(uint32_t ms) {
    uint32_t target = g_ticks + ms;
    while (g_ticks < target) __asm__ __volatile__("hlt");
}

uint32_t pit_uptime_ms(void) { return g_ticks; }
