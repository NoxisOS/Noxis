/**
 * @file    proc/scheduler.h
 * @brief   Round-robin preemptive scheduler (x86-64, kernel threads).
 * @author  Noxis Team
 */
#ifndef PROC_SCHEDULER_H
#define PROC_SCHEDULER_H

#include <proc/process.h>
#include <kernel/isr/isr.h>

extern process_t* g_current;
extern process_t* g_ready_head;

os_status_t scheduler_init(void);
void        scheduler_add(process_t* proc);
os_status_t scheduler_spawn(const uint8_t* name, void (*entry)(void), uint32_t priority);
process_t*  scheduler_current(void);
void        scheduler_yield(void);
void        scheduler_tick(isr_frame_t* frame);

#endif /* PROC_SCHEDULER_H */
