/**
 * @file    proc/process.h
 * @brief   Process structure + lifecycle (x86-64).
 *
 * NOTE: minimal during the 64-bit port — kernel threads only.
 * fork/exec, fd table, signals, VFS, arena will be re-added as those
 * subsystems are ported.
 * @author  Noxis Team
 */
#ifndef PROC_PROCESS_H
#define PROC_PROCESS_H

#include <common/types.h>
#include <common/status.h>

#define PROC_NAME_MAX     32
#define PROC_QUANTUM      10
#define PROC_KSTACK_SIZE  16384      /* 16 KB kernel stack per thread */

typedef enum {
    PROC_READY    = 0,
    PROC_RUNNING  = 1,
    PROC_BLOCKED  = 2,
    PROC_ZOMBIE   = 3,
} proc_state_t;

typedef struct process {
    uint64_t         pid;
    uint8_t          name[PROC_NAME_MAX];
    proc_state_t     state;
    uint64_t         kctx_rsp;       /* saved RSP for kthread_switch */
    uint64_t         kstack_base;    /* allocation base (to free later) */
    uint64_t         pml4;           /* address space (0 = kernel AS) */
    uint32_t         quantum_remaining;
    uint32_t         priority;
    struct process*  next;
} process_t;

process_t* proc_spawn(const uint8_t* name, void (*entry)(void), uint32_t priority);

#endif /* PROC_PROCESS_H */
