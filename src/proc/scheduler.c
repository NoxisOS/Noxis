/**
 * @file    proc/scheduler.c
 * @brief   Round-robin preemptive scheduler (x86-64 kernel threads).
 *
 * Minimal during the 64-bit port: a circular ready list of kernel
 * threads, preempted by the PIT.  No blocking/zombie/fork yet.
 */
#include <proc/scheduler.h>
#include <proc/process.h>
#include <mm/virt/vmm.h>
#include <kernel/hal/gdt.h>
#include <kernel/syscall/syscall.h>
#include <common/types.h>

extern void kthread_switch(uint64_t* save_old_rsp, uint64_t new_rsp);

process_t* g_current;
process_t* g_ready_head;
static process_t* g_ready_tail;
static process_t* g_all_head;      /* every live process (for waitpid)   */
static process_t  g_idle;          /* the bootstrap context becomes idle */

os_status_t scheduler_init(void) {
    g_ready_head = g_ready_tail = NULL;
    /* The currently-running boot context is process 0 (idle). */
    for (int i = 0; i < PROC_NAME_MAX; i++) g_idle.name[i] = 0;
    g_idle.pid = 0;
    g_idle.state = PROC_RUNNING;
    g_idle.quantum_remaining = PROC_QUANTUM;
    g_idle.next  = NULL;
    g_current = &g_idle;
    return OS_OK;
}

void scheduler_add(process_t* p) {
    p->next = NULL;
    if (g_ready_tail) g_ready_tail->next = p;
    else              g_ready_head = p;
    g_ready_tail = p;
}

/* Register a brand-new process: track it globally and make it runnable. */
void scheduler_register(process_t* p) {
    p->all_next = g_all_head;
    g_all_head  = p;
    scheduler_add(p);
}

process_t* scheduler_find(uint64_t pid) {
    for (process_t* p = g_all_head; p; p = p->all_next)
        if (p->pid == pid) return p;
    return NULL;
}

/* Return the idx-th process in the global list (for ps/procinfo), or NULL. */
process_t* scheduler_at(uint32_t idx) {
    for (process_t* p = g_all_head; p; p = p->all_next)
        if (idx-- == 0) return p;
    return NULL;
}

/* Unlink a (dead) process from the global list so ps stops showing it. */
void scheduler_remove(process_t* p) {
    process_t** pp = &g_all_head;
    while (*pp) {
        if (*pp == p) { *pp = p->all_next; return; }
        pp = &(*pp)->all_next;
    }
}

/* Terminate the current process: record its code, mark it a zombie (so yield
   won't requeue it), and switch away for good. */
void scheduler_exit(int code) {
    g_current->exit_code = code;
    g_current->state     = PROC_ZOMBIE;
    scheduler_yield();                 /* never returns to this context */
    for (;;) __asm__ __volatile__("cli; hlt");
}

/* Reap a finished child: scan zombies parented by `parent`, matching `pid`
   when pid > 0.  Returns the child (still in the list) or NULL if none yet. */
process_t* scheduler_reap(process_t* parent, int64_t pid) {
    for (process_t* p = g_all_head; p; p = p->all_next) {
        if (p->state != PROC_ZOMBIE || p->parent != parent) continue;
        if (pid > 0 && p->pid != (uint64_t)pid) continue;
        return p;
    }
    return NULL;
}

os_status_t scheduler_spawn(const uint8_t* name, void (*entry)(void), uint32_t priority) {
    process_t* p = proc_spawn(name, entry, priority);
    if (!p) return OS_ERR_OOM;
    scheduler_add(p);
    return OS_OK;
}

process_t* scheduler_current(void) { return g_current; }

/* Pick the next ready thread, requeue the current one, and switch. */
void scheduler_yield(void) {
    if (!g_ready_head) return;            /* nobody else to run */

    process_t* prev = g_current;
    process_t* next = g_ready_head;
    g_ready_head = next->next;
    if (!g_ready_head) g_ready_tail = NULL;

    /* Requeue prev if it's still runnable (idle pid 0 also recirculates). */
    if (prev->state == PROC_RUNNING || prev->state == PROC_READY) {
        prev->state = PROC_READY;
        scheduler_add(prev);
    }

    next->state = PROC_RUNNING;
    next->quantum_remaining = PROC_QUANTUM;
    g_current = next;

    /* Switch address space if the next thread has its own (0 = kernel AS).
       Safe here: kernel stacks live in the physmap, shared by every AS. */
    if (next->pml4 != prev->pml4)
        vmm_switch(next->pml4 ? next->pml4 : vmm_kernel_pml4());

    /* Point TSS.rsp0 (interrupts) and g_cur_kstack (syscalls) at the next
       thread's kernel stack so a ring-3 → ring-0 transition lands correctly. */
    if (next->kstack_top) { gdt_set_kernel_stack(next->kstack_top);
                            g_cur_kstack = next->kstack_top; }

    kthread_switch(&prev->kctx_rsp, next->kctx_rsp);
}

void scheduler_tick(isr_frame_t* frame) {
    (void)frame;
    if (!g_current) return;
    if (g_current->quantum_remaining > 0) g_current->quantum_remaining--;
    if (g_current->quantum_remaining == 0) scheduler_yield();
}
