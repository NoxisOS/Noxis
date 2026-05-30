/**
 * @file    kernel/panic.h
 * @brief   Kernel panic handler
 * @author  Noxis Team
 * @date    2026-05-29
 */
#ifndef KERNEL_PANIC_H
#define KERNEL_PANIC_H

#include <common/types.h>
#include <kernel/isr.h>

/**
 * @brief Halts the system with a fatal error message on VGA
 * @param msg    Human-readable error description
 * @param frame  ISR frame (can be NULL if not from an interrupt)
 */
void kernel_panic(const uint8_t* msg, isr_frame_t* frame);

#endif /* KERNEL_PANIC_H */
