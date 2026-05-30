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
#include <proc/exec.h>

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

/**
 * @brief Marks the current process as ZOMBIE and switches to the next
 *        ready thread.  Does not return.  Call after setting exit_code
 *        and waking any waiter.
 */
void scheduler_exit(void) __attribute__((noreturn));

/**
 * @brief Returns the process with the given PID, searching all queues.
 *        Returns NULL if not found.
 */
process_t* scheduler_find_proc(uint32_t pid);

/**
 * @brief Creates a fork child of the calling process.
 *        Copies the parent's address space, sets up a kernel thread that
 *        will resume the child in ring 3 at the fork call-site with EAX=0.
 * @param frame  The sysenter frame captured at the sys_fork call.
 * @return child PID on success, (uint32_t)-1 on OOM.
 */
uint32_t scheduler_fork_spawn(isr_frame_t* frame);

#endif /* PROC_SCHEDULER_H */
