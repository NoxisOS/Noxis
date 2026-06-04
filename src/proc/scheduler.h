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
void        scheduler_register(process_t* proc);     /* track + make runnable */
process_t*  scheduler_find(uint64_t pid);
process_t*  scheduler_at(uint32_t idx);
process_t*  scheduler_reap(process_t* parent, int64_t pid);
void        scheduler_remove(process_t* proc);
void        scheduler_exit(int code);                /* terminate current     */
os_status_t scheduler_spawn(const uint8_t* name, void (*entry)(void), uint32_t priority);
process_t*  scheduler_current(void);
void        scheduler_yield(void);
void        scheduler_tick(isr_frame_t* frame);

/* Foreground process management (Ctrl-C / SIGINT delivery). */
void        scheduler_set_fg(uint64_t pid);   /* set the foreground pid        */
void        scheduler_sigint_fg(void);        /* send SIGINT to foreground pid */

#endif /* PROC_SCHEDULER_H */
