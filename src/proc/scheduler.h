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
 * @brief Create a kernel thread and add it to the ready queue.
 * @param name      human-readable label
 * @param entry     entry function (must never return)
 * @param priority  0 = highest
 */
os_status_t scheduler_spawn(const uint8_t* name, void (*entry)(void),
                            uint32_t priority);

/**
 * @brief Returns the currently running process
 */
process_t* scheduler_current(void);

/**
 * @brief Blocks the current thread for at least `ms` milliseconds.
 *        Yields the CPU so other threads can run (0% CPU).
 */
void thread_sleep(uint32_t ms);

#endif /* PROC_SCHEDULER_H */
