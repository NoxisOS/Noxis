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
#include <proc/exec.h>
#include <kernel/isr/isr.h>
#include <kernel/hal/ports.h>
#include <kernel/hal/gdt.h>
#include <drivers/pit.h>
#include <mm/virt/vmm.h>
#include <fs/pipe/pipe.h>
#include <common/types.h>

extern void kthread_switch(uint32_t* old_esp, uint32_t* new_esp);
extern void user_enter_fork(uint32_t entry, uint32_t stack);
extern void msr_write(uint32_t msr, uint32_t lo, uint32_t hi);

#define MSR_SYSENTER_ESP  0x175u
#define KERNEL_PD_PHYS    0x400000u

/* Switch to `next` and update TSS + SYSENTER_ESP so sysenter uses
   the correct kernel stack for whatever process runs next. */
#define DO_SWITCH(prev, next) do {                              \
    gdt_set_kernel_stack((next)->kstack_top);                   \
    msr_write(MSR_SYSENTER_ESP, (next)->kstack_top, 0);         \
    kthread_switch(&(prev)->kctx_esp, &(next)->kctx_esp);       \
} while (0)

/* ── file-scope state ──────────────────────────────────────── */
/* Non-static so syscall.c can access them for inline blocking (waitpid). */
process_t* g_current;
process_t* g_ready_head;
process_t* g_ready_tail;
process_t* g_blocked_head;

/* ── forward declarations ──────────────────────────────────── */
static void _scheduler_wake_expired(void);
static void _scheduler_unblock(process_t* p);

/* ── public functions ──────────────────────────────────────── */

os_status_t scheduler_init(void) {
    g_ready_head   = (process_t*)0;
    g_ready_tail   = (process_t*)0;
    g_blocked_head = (process_t*)0;

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

/* ── blocked-thread helpers ────────────────────────────────── */

static void _scheduler_wake_expired(void) {
    uint32_t now = pit_uptime_ms();
    process_t** prevp = &g_blocked_head;
    while (*prevp) {
        process_t* b = *prevp;
        if (b->wake_tick != 0 && b->wake_tick <= now) {
            *prevp = b->next;
            b->next    = (process_t*)0;
            b->wake_tick = 0;

            if (b == g_current) {
                /* Thread is the one currently executing (spinning in a hlt
                   loop inside thread_sleep).  Just mark it RUNNING so the
                   loop condition fires and exits; do NOT enqueue it into
                   g_ready_head or the scheduler will find it there on the
                   next tick and do kthread_switch(x,x), leaving it in the
                   ready queue for every subsequent sleep call. */
                b->state = PROC_RUNNING;
            } else {
                b->state = PROC_READY;
                if (!g_ready_head) {
                    g_ready_head = b;
                    g_ready_tail = b;
                } else {
                    g_ready_tail->next = b;
                    g_ready_tail       = b;
                }
            }
        } else {
            prevp = &b->next;
        }
    }
}

static void _scheduler_unblock(process_t* p) {
    process_t** prevp = &g_blocked_head;
    while (*prevp) {
        if (*prevp == p) {
            *prevp = p->next;
            p->next = NULL;
            p->wake_tick = 0;
            return;
        }
        prevp = &(*prevp)->next;
    }
}

void thread_sleep(uint32_t ms) {
    if (!g_current || ms == 0) return;

    __asm__ __volatile__("cli");
    g_current->wake_tick = pit_uptime_ms() + ms;
    g_current->state     = PROC_BLOCKED;

    g_current->next  = g_blocked_head;
    g_blocked_head   = g_current;

    if (g_ready_head) {
        process_t* next = g_ready_head;
        g_ready_head = next->next;
        if (!g_ready_head) g_ready_tail = (process_t*)0;
        next->next = (process_t*)0;

        process_t* prev = g_current;
        next->state = PROC_RUNNING;
        g_current   = next;
        DO_SWITCH(prev, next);
        __asm__ __volatile__("sti");
    } else {
        while (g_current->wake_tick > pit_uptime_ms()) {
            __asm__ __volatile__("sti; hlt; cli");
        }
        _scheduler_unblock(g_current);
        g_current->wake_tick = 0;
        g_current->state     = PROC_RUNNING;
        __asm__ __volatile__("sti");
    }
}

/* ── generic wait-channel primitives ──────────────────────── */

void scheduler_wake(process_t** waiter) {
    if (!*waiter) return;

    if (*waiter == g_current) {
        /* The waiter is the currently-executing thread (hlt-loop case
           inside scheduler_block_on).  Just clear the pointer — the
           loop will see it and break. */
        *waiter = (process_t*)0;
    } else {
        scheduler_add(*waiter);
        *waiter = (process_t*)0;
    }
}

void scheduler_block_on(process_t** waiter) {
    /* Disable interrupts while we atomically set the waiter + dequeue
       from the ready queue.  An ISR (keyboard, PIT) calling scheduler_wake
       or scheduler_tick must not see a half-updated g_ready_head. */
    __asm__ __volatile__("cli");

    *waiter = g_current;
    g_current->state     = PROC_BLOCKED;
    g_current->wake_tick = 0;

    if (g_ready_head) {
        process_t* next = g_ready_head;
        g_ready_head = next->next;
        if (!g_ready_head) g_ready_tail = (process_t*)0;
        next->next = (process_t*)0;

        process_t* prev = g_current;
        next->state = PROC_RUNNING;
        g_current   = next;
        DO_SWITCH(prev, next);
        /* We resume here when scheduler_wake adds us back to the
           ready queue.  IF=0 from DO_SWITCH's kthread_switch path. */
        __asm__ __volatile__("sti");
    } else {
        /* Nothing else to run — spin with hlt until scheduler_wake
           fires from an ISR and clears the waiter pointer. */
        for (;;) {
            __asm__ __volatile__("sti; hlt; cli");
            if (*waiter == (process_t*)0) break;
        }
        __asm__ __volatile__("sti");
    }
}

/* ── scheduler_tick ────────────────────────────────────────── */
void scheduler_tick(isr_frame_t* frame) {
    if (!g_current || !frame) return;

    /* Never preempt ring-3 user processes (CS RPL bits = 3). */
    if ((frame->cs & 3) != 0) return;

    /* Wake any threads whose sleep timers have expired. */
    _scheduler_wake_expired();

    /* When the current thread is blocked (e.g. sleeping), skip the
       normal round-robin and switch to the next ready thread. */
    if (g_current->state == PROC_BLOCKED) {
        if (g_ready_head) {
            process_t* next = g_ready_head;
            g_ready_head = next->next;
            if (!g_ready_head) g_ready_tail = (process_t*)0;
            next->next = (process_t*)0;

            if (next == g_current) {
                g_current->state = PROC_RUNNING;
                g_current->quantum_remaining = PROC_QUANTUM;
            } else {
                process_t* prev = g_current;
                next->state = PROC_RUNNING;
                g_current   = next;
                DO_SWITCH(prev, next);
            }
        }
        return;
    }

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

    DO_SWITCH(prev, next);
}

/* ── fork / wait / exit ─────────────────────────────────────── */

/* Entry point for fork children.
   Runs as a kernel thread, sets up the per-child address space, then
   drops into ring 3 at the same EIP/ESP as the parent had at fork time. */
static void _fork_child_entry(void) {
    process_t* me = scheduler_current();

    /* Point TSS + SYSENTER_ESP at our kernel stack so sysenter works. */
    gdt_set_kernel_stack(me->kstack_top);
    msr_write(MSR_SYSENTER_ESP, me->kstack_top, 0);

    /* Switch to our own page directory. */
    if (me->page_dir_phys)
        vmm_switch_pd(me->page_dir_phys);

    /* Jump to ring 3.  EAX is zeroed by user_enter_fork → fork() == 0. */
    user_enter_fork(me->fork_eip, me->fork_esp);

    /* Never reached. */
    for (;;) __asm__ __volatile__("hlt");
}

uint32_t scheduler_fork_spawn(isr_frame_t* frame) {
    process_t* parent = g_current;

    uint32_t parent_pd = exec_current_pd();
    if (!parent_pd) parent_pd = parent->page_dir_phys;
    if (!parent_pd) return (uint32_t)-1;

    /* ── Create child process FIRST ──────────────────────
       proc_spawn maps the child's kernel stack in the CURRENT PD
       (= exec PD) AND in the kernel PD (see process.c).  The child PD
       created below inherits these mappings from the current PD clone. */
    static const uint8_t child_name[] = "fork-child";
    process_t* child = proc_spawn(child_name, _fork_child_entry, parent->priority);
    if (!child) return (uint32_t)-1;

    /* ── Fork address space (AFTER proc_spawn so child kstack is included) */
    uint32_t child_pd;
    if (vmm_fork_pd(parent_pd, &child_pd) != OS_OK) return (uint32_t)-1;

    child->page_dir_phys = child_pd;
    child->is_fork_child = TRUE;
    child->ppid          = parent->pid;
    child->fork_eip      = frame->eip;      /* sysenter return addr (user EDX) */
    child->fork_esp      = frame->user_esp; /* user ESP (user ECX at sysenter)  */

    /* ── Inherit parent's open file descriptors ───────────────
       The fd_table lives in the kernel process_t (not in user memory),
       so vmm_fork_pd does NOT copy it.  We do it explicitly here.
       For pipe fds, increment the pipe's refcount so that the child
       closing its end doesn't prematurely signal EOF to the other end. */
    for (uint32_t i = 0; i < PROC_MAX_FD; i++) {
        child->fd_table[i] = parent->fd_table[i];
        if (parent->fd_table[i].used &&
            parent->fd_table[i].type == FD_PIPE &&
            parent->fd_table[i].pipe) {
            parent->fd_table[i].pipe->refs++;
        }
    }

    scheduler_add(child);
    return child->pid;
}

void scheduler_exit(void) {
    /* Current process must already be ZOMBIE + exit_code set by caller. */
    __asm__ __volatile__("cli");

    if (!g_ready_head) {
        /* Nothing else to run. Switch to kernel PD, destroy our PD, halt. */
        uint32_t my_pd = g_current->page_dir_phys;
        if (my_pd && my_pd != KERNEL_PD_PHYS) {
            vmm_switch_pd(KERNEL_PD_PHYS);
            vmm_destroy_pd(my_pd);
        }
        for (;;) __asm__ __volatile__("sti; hlt");
    }

    process_t* next = g_ready_head;
    g_ready_head = next->next;
    if (!g_ready_head) g_ready_tail = (process_t*)0;
    next->next = (process_t*)0;

    process_t* prev = g_current;
    next->state = PROC_RUNNING;
    g_current   = next;

    /* Switch to NEXT's address space (exec PD for the waiting parent) so the
       parent can immediately run its user code when kthread_switch resumes it.
       THEN destroy the child's PD (after we've left it). */
    uint32_t next_pd = next->page_dir_phys ? next->page_dir_phys : KERNEL_PD_PHYS;
    uint32_t prev_pd = prev->page_dir_phys;
    vmm_switch_pd(next_pd);
    if (prev_pd && prev_pd != KERNEL_PD_PHYS && prev_pd != next_pd)
        vmm_destroy_pd(prev_pd);
    prev->page_dir_phys = 0;

    DO_SWITCH(prev, next);
    for (;;) __asm__ __volatile__("hlt");
}

process_t* scheduler_find_proc(uint32_t pid) {
    if (g_current && g_current->pid == pid) return g_current;

    /* Search ready queue. */
    process_t* p = g_ready_head;
    while (p) { if (p->pid == pid) return p; p = p->next; }

    /* Search blocked queue. */
    p = g_blocked_head;
    while (p) { if (p->pid == pid) return p; p = p->next; }

    return (process_t*)0;
}
