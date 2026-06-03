/**
 * @file    proc/process.c
 * @brief   Process creation (x86-64 kernel threads).
 */
#include <proc/process.h>
#include <mm/virt/heap.h>
#include <common/types.h>

extern void thread_trampoline(void);   /* kthread_switch.asm */

static uint64_t g_next_pid = 1;

process_t* proc_spawn(const uint8_t* name, void (*entry)(void), uint32_t priority) {
    process_t* p = (process_t*)kmalloc(sizeof(process_t));
    if (!p) return NULL;

    uint64_t stack = (uint64_t)kmalloc(PROC_KSTACK_SIZE);
    if (!stack) { kfree(p); return NULL; }

    p->pid   = g_next_pid++;
    p->state = PROC_READY;
    p->quantum_remaining = PROC_QUANTUM;
    p->priority    = priority;
    p->kstack_base = stack;
    p->pml4  = 0;                /* kernel address space by default */
    p->next  = NULL;
    for (int i = 0; i < PROC_NAME_MAX - 1 && name[i]; i++) p->name[i] = name[i];
    p->name[PROC_NAME_MAX - 1] = 0;

    /* Build the initial kernel stack. kthread_switch restores callee-saved
       regs then `ret`s: we land in thread_trampoline with RBX = real entry.
       Layout (low→high): r15,r14,r13,r12,rbp,rbx(=entry), ret=trampoline. */
    uint64_t* sp = (uint64_t*)(stack + PROC_KSTACK_SIZE);
    *--sp = (uint64_t)thread_trampoline;  /* ret target */
    *--sp = (uint64_t)entry;              /* rbx → real entry */
    *--sp = 0;                            /* rbp */
    *--sp = 0;                            /* r12 */
    *--sp = 0;                            /* r13 */
    *--sp = 0;                            /* r14 */
    *--sp = 0;                            /* r15 */
    p->kctx_rsp = (uint64_t)sp;

    return p;
}
