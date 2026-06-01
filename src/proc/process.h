/**
 * @file    proc/process.h
 * @brief   Process structure and lifecycle
 * @author  Noxis Team
 * @date    2026-05-29
 */
#ifndef PROC_PROCESS_H
#define PROC_PROCESS_H

#include <common/types.h>
#include <common/status.h>
#include <common/signal.h>
#include <fs/vfs/vfs.h>
#include <mm/arena.h>

/* ── constants ─────────────────────────────────────────────── */
#define PROC_NAME_MAX    32
#define PROC_QUANTUM     10          /* ticks per time slice */
#define PROC_KSTACK_PAGES 2          /* 8 KB kernel stack */
#define PROC_MAX_FD      16          /* max open files per process */

/* ── process states ────────────────────────────────────────── */
typedef enum {
    PROC_READY    = 0,
    PROC_RUNNING  = 1,
    PROC_BLOCKED  = 2,
    PROC_ZOMBIE   = 3,
} proc_state_t;

/* ── saved CPU context ─────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint32_t edi, esi, ebp, ebx, edx, ecx, eax;
    uint32_t eip, cs, eflags;
    uint32_t esp, ss;
} context_t;

/* ── open file descriptor slot ─────────────────────────────── */
#define FD_FILE  0
#define FD_PIPE  1

struct pipe;
typedef struct {
    uint8_t      type;   /* FD_FILE or FD_PIPE */
    union {
        vfs_file_t* file;
        struct pipe* pipe;
    };
    uint32_t     pos;
    bool_t       used;
} opened_file_t;

/* ── process ───────────────────────────────────────────────── */
typedef struct process {
    uint32_t         pid;
    uint8_t          name[PROC_NAME_MAX];
    proc_state_t     state;
    context_t        ctx;           /* legacy — kept for ring-3 user processes */
    uint32_t         kctx_esp;      /* saved ESP for kthread_switch (ring-0)   */
    uint32_t         quantum_remaining;
    uint32_t         priority;
    uint32_t         kstack_top;    /* virtual addr of kernel stack top */
    struct process*  next;          /* ready/blocked-queue linked list */
    uint32_t         wake_tick;     /* g_ticks to wake at (0 = not sleeping) */
    opened_file_t    fd_table[PROC_MAX_FD];

    /* ── address space ──────────────────────────────────────── */
    uint32_t         page_dir_phys; /* physical addr of this process's PD
                                       (0 = use kernel PD, set for exec/fork) */

    /* ── fork / wait ────────────────────────────────────────── */
    uint32_t         ppid;          /* parent PID (0 = no parent)             */
    int32_t          exit_code;     /* stored when state → PROC_ZOMBIE        */
    struct process*  waiter;        /* process blocked in waitpid on us       */
    bool_t           is_fork_child; /* TRUE if created by sys_fork            */
    uint32_t         fork_eip;      /* ring-3 EIP to resume at (fork child)   */
    uint32_t         fork_esp;      /* ring-3 ESP to resume at (fork child)   */
    uint32_t         fork_ebp;      /* ring-3 EBP to restore in fork child    */
    uint32_t         fork_ebx;      /* ring-3 EBX to restore in fork child    */
    uint32_t         fork_esi;      /* ring-3 ESI to restore in fork child    */
    uint32_t         fork_edi;      /* ring-3 EDI to restore in fork child    */

    /* ── signals ──────────────────────────────────────────────── */
    sigaction_t      sigactions[NSIG];
    uint32_t         sig_pending;
    uint32_t         sig_blocked;

    /* ── filesystem ───────────────────────────────────────────── */
    uint32_t         cwd_ino;       /* current working directory inode */

    /* ── user heap (brk) ──────────────────────────────────────── */
    uint32_t         brk_start;     /* heap floor: end of loaded ELF image */
    uint32_t         brk;           /* current program break (grows up)    */

    /* ── x87 FPU (lazy switching) ─────────────────────────────── */
    uint8_t          fpu_state[108];/* FNSAVE/FRSTOR image                 */
    bool_t           fpu_used;      /* TRUE once this process touched FPU  */

    /* ── per-process kernel arena ──────────────────────────────── */
    /* All kernel-side allocations whose lifetime == this process   */
    /* (e.g. argv copies in execve) come from here.                 */
    /* arena_destroy() is called automatically in proc_terminate(). */
    arena_t         *arena;
} process_t;

/* ── public functions ──────────────────────────────────────── */

/**
 * @brief Creates a new kernel process
 * @param name     Human-readable name
 * @param entry    Entry point function (never returns)
 * @param priority Priority (0 = highest)
 * @return Pointer to new process, or NULL
 */
process_t* proc_spawn(const uint8_t* name, void (*entry)(void), uint32_t priority);

/**
 * @brief Marks the current process as exited (never returns to it)
 */
void proc_exit(void);

/**
 * @brief Releases a dead (ZOMBIE) process: kernel stack frames, arena,
 *        and the process_t slab slot. Never call on the running process.
 */
void proc_destroy(process_t* proc);

#endif /* PROC_PROCESS_H */
