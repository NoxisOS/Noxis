/**
 * @file    kernel/syscall/sys_proc.c
 * @brief   Process syscalls: exit, fork, waitpid, execve, brk,
 *                            getpid, getppid, getuid
 */
#include "syscall_internal.h"

void sys_exit(isr_frame_t* frame) {
    proc_terminate((int)frame->ebx);
}

void sys_fork(isr_frame_t* frame) {
    frame->eax = scheduler_fork_spawn(frame);
}

void sys_waitpid(isr_frame_t* frame) {
    uint32_t    pid   = frame->ebx;
    process_t*  me    = scheduler_current();
    process_t*  child = scheduler_find_proc(pid);

    if (!child) { frame->eax = (uint32_t)-1; return; }

    if (child->state == PROC_ZOMBIE) {
        frame->eax = (uint32_t)child->exit_code;
        return;
    }

    /* Block until child exits.  Must be atomic wrt scheduler_tick. */
    __asm__ __volatile__("cli");
    child->waiter = me;
    me->state     = PROC_BLOCKED;
    me->wake_tick = 0;

    if (g_ready_head) {
        process_t* next  = g_ready_head;
        g_ready_head     = next->next;
        if (!g_ready_head) g_ready_tail = (process_t*)0;
        next->next       = (process_t*)0;

        process_t* prev  = me;
        next->state      = PROC_RUNNING;
        g_current        = next;

        gdt_set_kernel_stack(next->kstack_top);
        msr_write(MSR_SYSENTER_ESP, next->kstack_top, 0);
        kthread_switch(&prev->kctx_esp, &next->kctx_esp);
    }
    __asm__ __volatile__("sti");
    frame->eax = (uint32_t)child->exit_code;
}

void sys_execve(isr_frame_t* frame) {
    const uint8_t* upath = (const uint8_t*)frame->ebx;
    char**         uargv = (char**)frame->esi;

    if (!upath || !_user_range_ok(frame->ebx, 1))
        { frame->eax = (uint32_t)-1; return; }

    /* ── Copy path ─────────────────────────────────────────── */
    uint8_t  name[64];
    uint32_t nlen = 0;
    for (; nlen < sizeof(name) - 1 && upath[nlen]; nlen++)
        name[nlen] = upath[nlen];
    name[nlen] = '\0';

    /* ── Copy argv strings into flat kernel buffer ─────────── */
#define KA_MAX  16
#define KBF_SZ  512
    uint8_t  kbuf[KBF_SZ];
    uint32_t kptrs[KA_MAX];
    uint32_t kargc = 0;
    uint32_t kpos  = 0;

    if (uargv && _user_range_ok(frame->esi, 4)) {
        for (kargc = 0; kargc < KA_MAX; kargc++) {
            uint32_t paddr = frame->esi + kargc * 4;
            if (!_user_range_ok(paddr, 4)) break;
            const uint8_t* uarg = (const uint8_t*)uargv[kargc];
            if (!uarg) break;
            if (!_user_range_ok((uint32_t)uarg, 1)) break;
            kptrs[kargc] = kpos;
            while (kpos < KBF_SZ - 1 && *uarg)
                kbuf[kpos++] = *uarg++;
            kbuf[kpos++] = '\0';
        }
    }
    if (kargc == 0) {
        kptrs[0] = 0;
        for (uint32_t i = 0; i <= nlen; i++) kbuf[i] = name[i];
        kpos  = nlen + 1;
        kargc = 1;
    }
    const uint8_t* kargv[KA_MAX];
    for (uint32_t i = 0; i < kargc; i++) kargv[i] = kbuf + kptrs[i];
#undef KA_MAX
#undef KBF_SZ

    /* ── Locate ELF before tearing down address space ─────── */
    vfs_file_t* f = vfs_lookup(name);
    if (!f) { frame->eax = (uint32_t)-1; return; }

    /* ── Point of no return: free user PDEs 1..767 ─────────── */
    process_t* proc = scheduler_current();
    uint32_t*  pd   = (uint32_t*)0xFFFFF000u;
    for (uint32_t pde = 1; pde < 768; pde++) {
        if (!(pd[pde] & PAGE_PRESENT)) continue;
        uint32_t* pt = (uint32_t*)(0xFFC00000u + pde * PAGE_SIZE);
        for (uint32_t pte = 0; pte < 1024; pte++)
            if (pt[pte] & PAGE_PRESENT) pmm_free_frame(pt[pte] & ~0xFFFu);
        uint32_t pt_phys = pd[pde] & ~0xFFFu;
        pd[pde] = 0;
        vmm_invlpg((uint32_t)pt);
        pmm_free_frame(pt_phys);
    }

    /* ── Load new ELF ───────────────────────────────────────── */
    uint32_t entry, prog_end;
    if (elf_load(f->data, f->size, &entry, &prog_end) != OS_OK)
        proc_terminate(127);

    proc->brk_start = prog_end;
    proc->brk       = prog_end;

    /* ── Fresh user stack ───────────────────────────────────── */
    uint32_t stack_phys;
    if (pmm_alloc_frame(&stack_phys) != OS_OK ||
        vmm_map_page(USER_STACK_INIT, stack_phys,
                     PAGE_PRESENT | PAGE_RW | PAGE_USER) != OS_OK)
        proc_terminate(127);

    uint32_t sp = _build_argv_frame(kargc, kargv);

    gdt_set_kernel_stack(proc->kstack_top);
    msr_write(MSR_SYSENTER_ESP, proc->kstack_top, 0);
    user_enter(entry, sp);
}

void sys_brk(isr_frame_t* frame) {
    process_t* me  = scheduler_current();
    uint32_t   req = frame->ebx;

    if (req == 0) { frame->eax = me->brk; return; }

    if (req < me->brk_start || req > USER_HEAP_MAX) {
        frame->eax = me->brk;
        return;
    }
    me->brk    = req;
    frame->eax = me->brk;
}

void sys_getpid(isr_frame_t* frame) {
    frame->eax = scheduler_current()->pid;
}

void sys_getppid(isr_frame_t* frame) {
    frame->eax = scheduler_current()->ppid;
}

void sys_getuid(isr_frame_t* frame) {
    frame->eax = 0;   /* single-user — everything is root */
}
