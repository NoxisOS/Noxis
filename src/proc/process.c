/**
 * @file    proc/process.c
 * @brief   Process creation and management.
 *
 * Memory strategy
 * ───────────────
 * process_t structs come from the slab allocator (g_process_slab).
 * Each process owns a kernel-side arena (proc->arena) for short-lived
 * kernel allocations that have the same lifetime as the process.
 * Both are freed automatically in proc_terminate() → no leaks.
 */
#include <proc/process.h>
#include <mm/slab.h>
#include <mm/arena.h>
#include <mm/virt/heap.h>
#include <mm/phys/pmm.h>
#include <mm/virt/vmm.h>
#include <mm/virt/paging.h>
#include <common/types.h>

static uint32_t g_next_pid = 1;

process_t* proc_spawn(const uint8_t* name, void (*entry)(void), uint32_t priority) {

    /* ── Allocate process struct from the typed slab ─────────
       slab_alloc() returns a zeroed object in O(1).             */
    process_t* proc = (process_t*)slab_alloc(g_process_slab);
    if (!proc) return (process_t*)0;

    for (uint32_t i = 0; i < PROC_NAME_MAX - 1 && name[i]; i++)
        proc->name[i] = name[i];
    proc->name[PROC_NAME_MAX - 1] = '\0';

    proc->pid               = g_next_pid++;
    proc->state             = PROC_READY;
    proc->quantum_remaining = PROC_QUANTUM;
    proc->priority          = priority;
    proc->next              = (process_t*)0;

    /* ── Kernel stack ──────────────────────────────────────── */
    proc->kstack_top = 0;
    for (uint32_t i = 0; i < PROC_KSTACK_PAGES; i++) {
        uint32_t phys;
        if (pmm_alloc_frame(&phys) != OS_OK) {
            slab_free(g_process_slab, proc);
            return (process_t*)0;
        }
        uint32_t virt = 0xD0000000 + g_next_pid * 0x10000 + i * PAGE_SIZE;
        if (vmm_map_page(virt, phys, PAGE_PRESENT | PAGE_RW) != OS_OK) {
            slab_free(g_process_slab, proc);
            return (process_t*)0;
        }
        if (vmm_get_pd_phys() != 0x400000u)
            vmm_map_page_in(0x400000u, virt, phys, PAGE_PRESENT | PAGE_RW);

        if (i == PROC_KSTACK_PAGES - 1) proc->kstack_top = virt + PAGE_SIZE;
    }

    /* ── Per-process kernel arena ──────────────────────────── */
    proc->arena = arena_create();
    /* Arena failure is not fatal — kernel falls back to kmalloc
       for individual allocs if proc->arena is NULL.            */

    /* ── CPU context ───────────────────────────────────────── */
    proc->ctx.eip    = (uint32_t)entry;
    proc->ctx.cs     = 0x08;
    proc->ctx.eflags = 0x202;
    proc->ctx.esp    = proc->kstack_top;
    proc->ctx.ss     = 0x10;

    extern void kthread_entry(void);
    volatile uint32_t* sp = (volatile uint32_t*)proc->kstack_top;
    *--sp = (uint32_t)entry;
    *--sp = (uint32_t)kthread_entry;
    *--sp = 0;  /* ebp */
    *--sp = 0;  /* esi */
    *--sp = 0;  /* edi */
    *--sp = 0;  /* ebx */
    proc->kctx_esp = (uint32_t)sp;

    /* ── Signals: default disposition ─────────────────────── */
    for (uint32_t i = 0; i < NSIG; i++) {
        proc->sigactions[i].handler = SIG_DFL;
        proc->sigactions[i].flags   = 0;
    }

    /* fd_table is already zeroed by slab_alloc(). */

    return proc;
}

/* ── proc_destroy ─────────────────────────────────────────────
   Release a dead process's resources:
     • kernel-stack frames (mapped in the kernel PD at 0x400000)
     • the per-process kernel arena
     • the process_t slab slot
   Must NOT be called on the currently-running process (it would
   pull the kernel stack out from under itself).  Call it only to
   reap a ZOMBIE from a different process's context.              */
void proc_destroy(process_t* proc) {
    if (!proc) return;

    /* Free the kernel-stack pages.  They live at
       [kstack_top - PROC_KSTACK_PAGES*PAGE_SIZE, kstack_top)
       and were mapped into the kernel PD (physical 0x400000).    */
    if (proc->kstack_top) {
        uint32_t base = proc->kstack_top - PROC_KSTACK_PAGES * PAGE_SIZE;
        for (uint32_t i = 0; i < PROC_KSTACK_PAGES; i++)
            vmm_unmap_page_in(0x400000u, base + i * PAGE_SIZE);
        proc->kstack_top = 0;
    }

    /* Release the per-process kernel arena (if still present). */
    if (proc->arena) {
        arena_destroy(proc->arena);
        proc->arena = (arena_t*)0;
    }

    /* Return the struct to the slab. */
    slab_free(g_process_slab, proc);
}

void proc_exit(void) {
    for (;;);
}
