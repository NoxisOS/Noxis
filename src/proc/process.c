/**
 * @file    proc/process.c
 * @brief   Process creation and management
 * @author  Noxis Team
 * @date    2026-05-29
 */
#include <proc/process.h>
#include <mm/heap.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <mm/paging.h>
#include <common/types.h>

static uint32_t g_next_pid = 1;

process_t* proc_spawn(const uint8_t* name, void (*entry)(void), uint32_t priority) {
    process_t* proc = (process_t*)kmalloc(sizeof(process_t));
    if (!proc) return (process_t*)0;

    for (uint32_t i = 0; i < PROC_NAME_MAX - 1 && name[i]; i++)
        proc->name[i] = name[i];
    proc->name[PROC_NAME_MAX - 1] = '\0';

    proc->pid               = g_next_pid++;
    proc->state             = PROC_READY;
    proc->quantum_remaining = PROC_QUANTUM;
    proc->priority          = priority;
    proc->next              = (process_t*)0;

    /* Allocate kernel stack */
    proc->kstack_top = 0;
    for (uint32_t i = 0; i < PROC_KSTACK_PAGES; i++) {
        uint32_t phys;
        if (pmm_alloc_frame(&phys) != OS_OK) { kfree(proc); return (process_t*)0; }
        uint32_t virt = 0xD0000000 + g_next_pid * 0x10000 + i * PAGE_SIZE;
        if (vmm_map_page(virt, phys, PAGE_PRESENT | PAGE_RW) != OS_OK) { kfree(proc); return (process_t*)0; }
        if (i == 0) proc->kstack_top = virt + PAGE_SIZE;
    }

    /* Legacy ISR-frame context (used by ring-3 loader path). */
    proc->ctx.edi    = 0;
    proc->ctx.esi    = 0;
    proc->ctx.ebp    = 0;
    proc->ctx.ebx    = 0;
    proc->ctx.edx    = 0;
    proc->ctx.ecx    = 0;
    proc->ctx.eax    = 0;
    proc->ctx.eip    = (uint32_t)entry;
    proc->ctx.cs     = 0x08;
    proc->ctx.eflags = 0x202;
    proc->ctx.esp    = proc->kstack_top;
    proc->ctx.ss     = 0x10;

    /* kthread_switch context — pre-initialize kstack so the first switch
       into this thread goes through kthread_entry (re-enables IRQs) and
       then jumps to the actual entry function. */
    extern void kthread_entry(void);
    volatile uint32_t* sp = (volatile uint32_t*)proc->kstack_top;
    *--sp = (uint32_t)entry;         /* 2nd ret: actual thread fn   */
    *--sp = (uint32_t)kthread_entry; /* 1st ret: sti trampoline     */
    *--sp = 0;                        /* ebp                         */
    *--sp = 0;                        /* esi                         */
    *--sp = 0;                        /* edi                         */
    *--sp = 0;                        /* ebx                         */
    proc->kctx_esp = (uint32_t)sp;

    proc->wake_tick = 0;
    for (uint32_t i = 0; i < PROC_MAX_FD; i++) {
        proc->fd_table[i].used = FALSE;
        proc->fd_table[i].file = (void*)0;
        proc->fd_table[i].pos  = 0;
    }

    return proc;
}

void proc_exit(void) {
    for (;;);
}
