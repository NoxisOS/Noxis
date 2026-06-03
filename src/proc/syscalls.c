/**
 * @file    proc/syscalls.c
 * @brief   Process-management syscalls: fork, exec, exit, getpid, waitpid.
 */
#include <kernel/syscall/syscall64.h>
#include <proc/process.h>
#include <proc/scheduler.h>
#include <mm/virt/vmm.h>
#include <mm/phys/pmm.h>
#include <fs/vfs/vfs.h>
#include <common/types.h>

void serial_write(const uint8_t* s);
void serial_write_hex64(uint64_t v);
uint64_t elf64_load_into(uint64_t pml4_phys, const uint8_t* img);

#define USTACK_VA  0x50000000ULL

/* ── fork ──────────────────────────────────────────────────────
 * Duplicate the current process: copy its user address space, then build a
 * child kernel stack that, when first scheduled, `ret`s into fork_ret_trampoline
 * and sysrets to ring 3 with a cloned register frame (rax = 0).
 */
int64_t sys_fork(syscall_frame_t* f) {
    process_t* parent = scheduler_current();

    uint64_t child_pml4 = vmm_create_address_space();
    if (!child_pml4) return -1;
    if (vmm_copy_user_space(child_pml4, parent->pml4) != 0) return -1;

    process_t* child = proc_alloc(parent->name);
    if (!child) return -1;
    child->pml4   = child_pml4;
    child->parent = parent;
    for (int i = 0; i < PROC_MAX_FDS; i++) child->fds[i] = parent->fds[i];

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
int64_t sys_exec(syscall_frame_t* f, const uint8_t* path) {
    vfs_file_t* prog = vfs_lookup(path);
    if (!prog || !prog->data) return -1;

    uint64_t nas = vmm_create_address_space();
    if (!nas) return -1;
    uint64_t entry = elf64_load_into(nas, prog->data);
    if (!entry) return -1;
    vmm_map_page_into(nas, USTACK_VA, pmm_alloc_frame(), PAGE_RW | PAGE_USER);

    process_t* cur = scheduler_current();
    cur->pml4 = nas;
    vmm_switch(nas);                   /* kernel half is shared, frame stays valid */

    f->rip    = entry;
    f->ursp   = USTACK_VA + 0x1000;
    f->rflags = 0x202;                 /* IF=1 */
    f->rax    = 0;                     /* fresh program entry; rax don't-care */
    return 0;
}

void sys_exit(int code) {
    serial_write((const uint8_t*)"[noxis64] pid ");
    serial_write_hex64(scheduler_current()->pid);
    serial_write((const uint8_t*)" exit code=");
    serial_write_hex64((uint64_t)(uint32_t)code);
    serial_write((const uint8_t*)"\n");
    scheduler_exit(code);              /* never returns */
}

uint64_t sys_getpid(void) { return scheduler_current()->pid; }

int64_t sys_waitpid(int64_t pid, int* status) {
    process_t* parent = scheduler_current();
    for (;;) {
        process_t* z = scheduler_reap(parent, pid);
        if (z) {
            if (status) *status = z->exit_code;
            z->parent = NULL;          /* mark reaped (won't match again) */
            return (int64_t)z->pid;
        }
        scheduler_yield();             /* let the child run, then re-check */
    }
}
