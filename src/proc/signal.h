/**
 * @file    proc/signal.h
 * @brief   Signal delivery to processes returning to userspace
 * @author  Noxis Team
 * @date    2026-05-30
 */
#ifndef PROC_SIGNAL_H
#define PROC_SIGNAL_H

#include <kernel/isr.h>

/**
 * @brief Check and deliver pending signals to the current process.
 *        Must be called after syscall dispatch, before returning to ring 3.
 *        Modifies frame->eip and frame->user_esp if a handler is invoked.
 * @param frame  Interrupt stack frame (will be modified for signal delivery)
 */
void signal_deliver(isr_frame_t* frame);

#endif /* PROC_SIGNAL_H */
