/**
 * @file    proc/scheduler.h
 * @brief   Round-robin preemptive scheduler
 * @author  Noxis Team
 * @date    2026-05-29
 */
#ifndef PROC_SCHEDULER_H
#define PROC_SCHEDULER_H

#include <common/types.h>
#include <kernel/isr.h>
#include <proc/process.h>

/**
 * @brief Initializes the scheduler (creates idle task)
 * @return OS_OK on success
 */
os_status_t scheduler_init(void);

/**
 * @brief Called every PIT tick — may trigger context switch
 */
void scheduler_tick(isr_frame_t* frame);

/**
 * @brief Adds a process to the ready queue
 */
void scheduler_add(process_t* proc);

/**
 * @brief Returns the currently running process
 */
process_t* scheduler_current(void);

#endif /* PROC_SCHEDULER_H */
