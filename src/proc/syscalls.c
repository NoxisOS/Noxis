/**
 * @file    proc/syscalls.c
 * @brief   Process-management syscalls: fork, exec, exit, getpid, waitpid.
 */
#include <kernel/syscall/syscall.h>
#include <proc/process.h>
#include <proc/scheduler.h>
#include <mm/virt/vmm.h>
#include <mm/phys/pmm.h>
#include <mm/virt/heap.h>
#include <mm/virt/uvm.h>
#include <fs/vfs/vfs.h>
#include <common/types.h>

void serial_write(const uint8_t* s);
void serial_write_hex64(uint64_t v);
uint64_t elf64_load_into(uint64_t pml4_phys, const uint8_t* img);
void pipe_addref(int kind, void* file);
void pipe_close(int kind, void* file);
static int fd_is_pipe(int k) { return k == FD_PIPE_R || k == FD_PIPE_W; }

/* Stack layout comes from uvm.h (USTACK_BASE / USTACK_TOP / USTACK_LIMIT). */
#define MAX_ARGS      16

static uint64_t kstrlen(const uint8_t* s) { uint64_t n = 0; while (s[n]) n++; return n; }

/* ── fork ──────────────────────────────────────────────────────
 * Duplicate the current process: copy its user address space, then build a
 * child kernel stack that, when first scheduled, `ret`s into fork_ret_trampoline
 * and sysrets to ring 3 with a cloned register frame (rax = 0).
 */
int64_t sys_fork(syscall_frame_t* f) {
    process_t* parent = scheduler_current();

    uint64_t child_pml4 = vmm_create_address_space();
    if (!child_pml4) return -1;
    if (vmm_cow_user_space(child_pml4, parent->pml4) != 0) return -1;

    process_t* child = proc_alloc(parent->name);
    if (!child) return -1;
    child->pml4      = child_pml4;
    child->parent    = parent;
    child->stack_low = parent->stack_low;  /* child inherits parent's stack depth */
    for (int i = 0; i < PROC_MAX_FDS; i++) {
        child->fds[i] = parent->fds[i];
        if (fd_is_pipe(child->fds[i].kind))      /* extra reference per end */
            pipe_addref(child->fds[i].kind, child->fds[i].file);
    }
    child->sig_pending = 0;                      /* pending signals are not inherited */
    for (int i = 0; i < 32; i++) child->sig_handler[i] = parent->sig_handler[i];

    /* Cloned syscall frame at the top of the child's kernel stack. */
    syscall_frame_t* cf =
        (syscall_frame_t*)(child->kstack_top - sizeof(syscall_frame_t));
    *cf = *f;
    cf->rax = 0;                       /* the child sees fork() return 0 */

    /* Below it: a kthread_switch save area whose `ret` lands in the trampoline.
       kthread_switch pops r15,r14,r13,r12,rbp,rbx then ret → memory order
       (low→high): r15..rbx, ret. */
    uint64_t* sp = (uint64_t*)cf;
    *--sp = (uint64_t)fork_ret_trampoline;   /* ret target */
    *--sp = 0;   /* rbx */
    *--sp = 0;   /* rbp */
    *--sp = 0;   /* r12 */
    *--sp = 0;   /* r13 */
    *--sp = 0;   /* r14 */
    *--sp = 0;   /* r15 */
    child->kctx_rsp = (uint64_t)sp;

    scheduler_register(child);
    return (int64_t)child->pid;        /* parent gets the child's pid */
}

/* ── exec ──────────────────────────────────────────────────────
 * Replace the current process's image with the program at `path`. Builds a
 * fresh address space, loads the ELF, then rewrites the syscall frame so the
 * return path sysrets straight into the new program.
 */
int64_t sys_exec(syscall_frame_t* f, const uint8_t* path, const uint8_t** argv) {
    vfs_file_t* prog = vfs_lookup(path);
    if (!prog || !prog->data) return -1;

    uint64_t nas = vmm_create_address_space();
    if (!nas) return -1;
    uint64_t entry = elf64_load_into(nas, prog->data);
    if (!entry) return -1;

    uint64_t stk = pmm_alloc_frame();
    if (!stk) return -1;
    vmm_map_page_into(nas, USTACK_BASE, stk, PAGE_RW | PAGE_USER);

    /* Build the argv stack into the new page through the physmap (the physmap
       is shared, so argv from the *current* AS is still readable here — we have
       not switched CR3 yet). Layout at rsp: argc, argv[0..argc-1], NULL, strings. */
    uint8_t*  page = (uint8_t*)(PHYSMAP_BASE + stk);
    uint64_t  off  = 0x1000;
    uint64_t  argv_uva[MAX_ARGS];
    int       argc = 0;
    if (argv) for (; argc < MAX_ARGS && argv[argc]; argc++) { /* count */ }

    for (int i = argc - 1; i >= 0; i--) {               /* copy strings, top-down */
        uint64_t len = kstrlen(argv[i]) + 1;
        off -= len;
        for (uint64_t b = 0; b < len; b++) page[off + b] = argv[i][b];
        argv_uva[i] = USTACK_BASE + off;
    }
    off &= ~7ULL;                                       /* align */
    off -= 8; *(uint64_t*)(page + off) = 0;             /* argv[argc] = NULL */
    for (int i = argc - 1; i >= 0; i--) { off -= 8; *(uint64_t*)(page + off) = argv_uva[i]; }
    off -= 8; *(uint64_t*)(page + off) = (uint64_t)argc;/* argc (rsp points here) */

    process_t* cur = scheduler_current();
    uint64_t   old_pml4 = cur->pml4;
    cur->pml4 = nas;
    vmm_switch(nas);                   /* kernel half is shared, frame stays valid */
    vmm_free_user_space(old_pml4);     /* reclaim old pages now that we've switched */

    cur->stack_low = USTACK_BASE;      /* one page mapped; rest demand-paged */
    f->rip    = entry;
    f->ursp   = USTACK_BASE + off;
    f->rflags = 0x202;                 /* IF=1 */
    f->rax    = 0;
    return 0;
}

void sys_exit(int code) {
    /* Release any pipe references so readers see EOF / writers see broken pipe. */
    process_t* cur = scheduler_current();
    for (int i = 0; i < PROC_MAX_FDS; i++)
        if (fd_is_pipe(cur->fds[i].kind)) pipe_close(cur->fds[i].kind, cur->fds[i].file);

    serial_write((const uint8_t*)"[noxis64] pid ");
    serial_write_hex64(scheduler_current()->pid);
    serial_write((const uint8_t*)" exit code=");
    serial_write_hex64((uint64_t)(uint32_t)code);
    serial_write((const uint8_t*)"\n");
    scheduler_exit(code);              /* never returns */
}

uint64_t sys_getpid(void) { return scheduler_current()->pid; }

/* Copy the name of the idx-th VFS entry into namebuf (<=32). Returns 1 if it
   exists, 0 past the end — lets userland `ls` enumerate the directory. */
/* procinfo(idx, buf): fill { int64 pid; int64 state; char name[32] } for the
   idx-th process. Returns 1 if it exists, 0 past the end — the /proc payoff. */
int64_t sys_procinfo(uint64_t idx, uint8_t* buf) {
    process_t* p = scheduler_at((uint32_t)idx);
    if (!p) return 0;
    int64_t*  q = (int64_t*)buf;
    q[0] = (int64_t)p->pid;
    q[1] = (int64_t)p->state;
    uint8_t* nm = buf + 16;
    int i = 0; for (; p->name[i] && i < 31; i++) nm[i] = p->name[i]; nm[i] = 0;
    return 1;
}

int64_t sys_readdir(uint64_t idx, uint8_t* namebuf) {
    if (idx >= vfs_count()) return 0;
    vfs_file_t* f = vfs_entry((uint32_t)idx);
    if (!f) return 0;
    int i = 0;
    for (; f->name[i] && i < 31; i++) namebuf[i] = f->name[i];
    namebuf[i] = 0;
    return 1;
}

int64_t sys_waitpid(int64_t pid, int* status) {
    process_t* parent = scheduler_current();
    for (;;) {
        process_t* z = scheduler_reap(parent, pid);
        if (z) {
            if (status) *status = z->exit_code;
            int64_t cpid = (int64_t)z->pid;
            scheduler_remove(z);
            vmm_free_user_space(z->pml4);      /* release all user pages */
            kfree((void*)z->kstack_base);      /* kernel stack */
            kfree(z);                          /* process struct */
            return cpid;
        }
        scheduler_yield();             /* let the child run, then re-check */
    }
}
