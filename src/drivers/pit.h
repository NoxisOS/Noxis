/**
 * @file    drivers/pit.h
 * @brief   PIT (8253/8254) timer driver
 * @author  Noxis Team
 * @date    2026-05-29
 */
#ifndef DRIVERS_PIT_H
#define DRIVERS_PIT_H

#include <common/types.h>
#include <common/status.h>

/* ── constants ─────────────────────────────────────────────── */
#define PIT_BASE_FREQ  1193182

/* ── public functions ──────────────────────────────────────── */

/**
 * @brief Initializes PIT channel 0 at the given frequency
 * @param hz  Desired frequency (e.g., 1000 for 1 ms ticks)
 * @return OS_OK on success
 */
os_status_t pit_init(uint32_t hz);

/**
 * @brief Sleeps for the given number of milliseconds (busy-wait)
 * @param ms  Milliseconds to sleep
 */
void pit_sleep_ms(uint32_t ms);

/**
 * @brief Returns the number of ticks since boot
 */
uint32_t pit_uptime_ms(void);

#endif /* DRIVERS_PIT_H */
