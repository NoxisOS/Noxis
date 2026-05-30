/**
 * @file    proc/scheduler.c
 * @brief   Round-robin preemptive scheduler — kernel thread edition.
 *
 * Context switching uses kthread_switch (stack swap) for ring-0 kernel
 * threads. Ring-3 user processes are NOT preempted here (the sysenter
 * path blocks voluntarily via sys_read / sys_exit).
 *
 * How it works
 * ────────────
 * Every PIT tick calls scheduler_tick(frame). When the current thread's
 * quantum expires, kthread_switch(&current->kctx_esp, &next->kctx_esp)
 * saves callee-saved regs + ESP on the old stack and resumes on the new
 * stack — exactly where the new thread last called kthread_switch.
 *
 * The "main kernel thread" (shell) uses the stack from kernel_entry.asm.
 * Its kctx_esp is 0 at init; it gets saved on the very first switch.
 *
 * @author  Noxis Team
 * @date    2026-05-30
 */
#include <proc/scheduler.h>
#include <proc/process.h>
#include <kernel/isr.h>
#include <hal/ports.h>
#include <common/types.h>

extern void kthread_switch(uint32_t* old_esp, uint32_t* new_esp);

/* ── file-scope state ──────────────────────────────────────── */
static process_t* g_current;
static process_t* g_ready_head;
static process_t* g_ready_tail;

/* ── public functions ──────────────────────────────────────── */

os_status_t scheduler_init(void) {
    g_ready_head = (process_t*)0;
    g_ready_tail = (process_t*)0;

    /* Register the currently-running kernel context as the "main" thread.
       We give it a dummy process_t; kctx_esp=0 is overwritten on first switch. */
    process_t* main_thread = proc_spawn(
        (const uint8_t*)"shell", (void(*)(void))0, 0);
    if (!main_thread) return OS_ERR_OOM;

    main_thread->kctx_esp = 0;   /* will be set by kthread_switch on first switch */
    main_thread->state    = PROC_RUNNING;
    g_current = main_thread;

    return OS_OK;
}

os_status_t scheduler_spawn(const uint8_t* name, void (*entry)(void),
                            uint32_t priority) {
    process_t* p = proc_spawn(name, entry, priority);
    if (!p) return OS_ERR_OOM;
    scheduler_add(p);
    return OS_OK;
}

void scheduler_add(process_t* proc) {
    if (!proc) return;
    proc->next  = (process_t*)0;
    proc->state = PROC_READY;

    if (!g_ready_head) {
        g_ready_head = proc;
        g_ready_tail = proc;
    } else {
        g_ready_tail->next = proc;
        g_ready_tail       = proc;
    }
}

process_t* scheduler_current(void) { return g_current; }

void scheduler_tick(isr_frame_t* frame) {
    if (!g_current || !frame) return;

    /* Never preempt ring-3 user processes (CS RPL bits = 3). */
    if ((frame->cs & 3) != 0) return;

    /* Countdown quantum. */
    if (g_current->quantum_remaining > 0) {
        g_current->quantum_remaining--;
        return;
    }

    /* Nothing else to run — reset quantum and stay. */
    if (!g_ready_head) {
        g_current->quantum_remaining = PROC_QUANTUM;
        return;
    }

    /* Pick next thread. */
    process_t* next = g_ready_head;
    g_ready_head = next->next;
    if (!g_ready_head) g_ready_tail = (process_t*)0;
    next->next = (process_t*)0;

    /* Put current on ready queue. */
    process_t* prev = g_current;
    prev->state = PROC_READY;
    prev->quantum_remaining = PROC_QUANTUM;
    scheduler_add(prev);

    /* Commit switch. */
    next->state = PROC_RUNNING;
    g_current   = next;

    /* Stack swap — returns immediately on the new thread's stack.
       When prev is eventually switched back to, kthread_switch returns
       here and we unwind through scheduler_tick → _pit_isr → isr_common
       → iret on prev's own saved frame. */
    kthread_switch(&prev->kctx_esp, &next->kctx_esp);
}
