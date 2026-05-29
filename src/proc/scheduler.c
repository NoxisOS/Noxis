/**
 * @file    proc/scheduler.c
 * @brief   Round-robin scheduler using ISR frame manipulation
 * @author  Noxis Team
 * @date    2026-05-29
 */
#include <proc/scheduler.h>
#include <proc/process.h>
#include <kernel/isr.h>
#include <hal/ports.h>
#include <common/types.h>

/* ── file-scope state ──────────────────────────────────────── */
static process_t* g_current;
static process_t* g_ready_head;
static process_t* g_ready_tail;

/* ── private functions ─────────────────────────────────────── */

static void _idle_task(void) {
    for (;;) {
        cpu_hlt();
    }
}

/**
 * @brief Saves current process context from ISR frame
 */
static void _save_context(process_t* proc, isr_frame_t* frame) {
    proc->ctx.edi    = frame->edi;
    proc->ctx.esi    = frame->esi;
    proc->ctx.ebp    = frame->ebp;
    proc->ctx.ebx    = frame->ebx;
    proc->ctx.edx    = frame->edx;
    proc->ctx.ecx    = frame->ecx;
    proc->ctx.eax    = frame->eax;
    proc->ctx.eip    = frame->eip;
    proc->ctx.cs     = frame->cs;
    proc->ctx.eflags = frame->eflags;
    proc->ctx.esp    = frame->user_esp;
    proc->ctx.ss     = frame->user_ss;
}

/**
 * @brief Restores process context into ISR frame
 */
static void _restore_context(process_t* proc, isr_frame_t* frame) {
    frame->edi       = proc->ctx.edi;
    frame->esi       = proc->ctx.esi;
    frame->ebp       = proc->ctx.ebp;
    frame->ebx       = proc->ctx.ebx;
    frame->edx       = proc->ctx.edx;
    frame->ecx       = proc->ctx.ecx;
    frame->eax       = proc->ctx.eax;
    frame->eip       = proc->ctx.eip;
    frame->cs        = proc->ctx.cs;
    frame->eflags    = proc->ctx.eflags;
    frame->user_esp  = proc->ctx.esp;
    frame->user_ss   = proc->ctx.ss;
}

/* ── public functions ──────────────────────────────────────── */

os_status_t scheduler_init(void) {
    /* Create idle task */
    process_t* idle = proc_spawn((const uint8_t*)"idle", _idle_task, 7);
    if (!idle) return OS_ERR_OOM;

    g_current    = idle;
    g_ready_head = (process_t*)0;
    g_ready_tail = (process_t*)0;
    g_current->state = PROC_RUNNING;

    return OS_OK;
}

void scheduler_add(process_t* proc) {
    if (!proc) return;
    proc->next = (process_t*)0;
    proc->state = PROC_READY;

    if (!g_ready_head) {
        g_ready_head = proc;
        g_ready_tail = proc;
    } else {
        g_ready_tail->next = proc;
        g_ready_tail = proc;
    }
}

process_t* scheduler_current(void) {
    return g_current;
}

void scheduler_tick(isr_frame_t* frame) {
    if (!g_current || !frame) return;

    if (g_current->quantum_remaining > 0) {
        g_current->quantum_remaining--;
        return;
    }

    /* Quantum expired — switch */
    if (!g_ready_head) {
        g_current->quantum_remaining = PROC_QUANTUM;
        return;
    }

    /* Save current context */
    _save_context(g_current, frame);

    /* Move current to ready queue */
    g_current->state = PROC_READY;
    g_current->quantum_remaining = PROC_QUANTUM;
    scheduler_add(g_current);

    /* Pick next */
    process_t* next = g_ready_head;
    g_ready_head = next->next;
    if (!g_ready_head) g_ready_tail = (process_t*)0;
    next->next = (process_t*)0;
    next->state = PROC_RUNNING;

    /* Restore next context into ISR frame */
    _restore_context(next, frame);
    g_current = next;
}
