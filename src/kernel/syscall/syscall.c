/**
 * @file    syscall/syscall.c
 * @brief   System call dispatcher (int 0x80 + sysenter).
 *          Convention: EAX=#, EBX=arg1, ESI=arg2, ECX=arg3.
 * @author  Noxis Team
 * @date    2026-05-30
 */
#include <kernel/syscall/syscall.h>
#include <kernel/isr/isr.h>
#include <kernel/hal/idt.h>
#include <proc/scheduler.h>
#include <proc/process.h>
#include <proc/exec.h>
#include <proc/signal.h>
#include <drivers/vga.h>
#include <drivers/tty/tty.h>
#include <fs/vfs/vfs.h>
#include <fs/noxfs/noxfs.h>
#include <fs/pipe/pipe.h>
#include <common/types.h>
#include <common/signal.h>
#include <mm/virt/vmm.h>
#include <mm/virt/paging.h>
#include <mm/virt/uvm.h>
#include <mm/phys/pmm.h>
#include <proc/elf.h>

#define MSR_SYSENTER_CS   0x174
#define MSR_SYSENTER_ESP  0x175
#define MSR_SYSENTER_EIP  0x176

extern void isr_stub_128(void);
extern void sysenter_entry(void);
extern void msr_write(uint32_t msr, uint32_t low, uint32_t high);
extern void kthread_switch(uint32_t* old_esp, uint32_t* new_esp);
extern void gdt_set_kernel_stack(uint32_t esp);
extern void user_enter(uint32_t entry, uint32_t stack);

/* Scheduler internals needed for inline waitpid blocking. */
extern process_t* g_current;
extern process_t* g_ready_head;
extern process_t* g_ready_tail;

/* ── user-pointer validation ───────────────────────────────── */

#define USER_VIRT_BASE  0x00400000u
#define USER_VIRT_TOP   0xC0000000u

static int _user_range_ok(uint32_t ptr, uint32_t len) {
    if (ptr < USER_VIRT_BASE)   return 0;
    if (ptr + len < ptr)        return 0;  /* overflow */
    if (ptr + len > USER_VIRT_TOP) return 0;
    return 1;
}

/* ── syscall handlers ────────────────────────────────────────── */

static void _sys_write(isr_frame_t* frame) {
    uint32_t fd  = frame->ebx;
    const uint8_t* buf = (const uint8_t*)frame->esi;
    uint32_t len = frame->edi;
    if (len == 0) return;

    if (!_user_range_ok(frame->esi, len)) {
        frame->eax = (uint32_t)-1;
        return;
    }

    if (fd == STDOUT_FD || fd == STDERR_FD || fd == 0) {
        /* stdout / stderr → VGA */
        for (uint32_t i = 0; i < len; i++) {
            uint8_t c = buf[i];
            if (c == 0) break;
            vga_put_char(c);
        }
        frame->eax = len;
        return;
    }

    /* Pipe-backed fd. */
    process_t* proc = scheduler_current();
    if (fd < PROC_MAX_FD && proc->fd_table[fd].used &&
        proc->fd_table[fd].type == FD_PIPE) {
        int32_t n = pipe_write(proc->fd_table[fd].pipe, buf, len);
        frame->eax = n >= 0 ? (uint32_t)n : (uint32_t)-1;
        return;
    }

    /* File-backed fd. */
    if (fd < PROC_MAX_FD && proc->fd_table[fd].used) {
        vfs_file_t* f = proc->fd_table[fd].file;
        int32_t n = vfs_write_file(f, proc->fd_table[fd].pos,
                                   buf, len);
        if (n > 0) proc->fd_table[fd].pos += (uint32_t)n;
        frame->eax = n > 0 ? (uint32_t)n : (uint32_t)-1;
        return;
    }

    frame->eax = (uint32_t)-1;
}

static void _sys_exit(isr_frame_t* frame) {
    /* All teardown (fork-child zombie vs. main-exec return, fd cleanup,
       SIGCHLD) lives in proc_terminate — shared with the #PF handler. */
    proc_terminate((int)frame->ebx);
}

static void _sys_fork(isr_frame_t* frame) {
    uint32_t child_pid = scheduler_fork_spawn(frame);
    /* Parent: EAX = child PID.  Child: resumes via user_enter_fork with EAX=0. */
    frame->eax = child_pid;
}

static void _sys_waitpid(isr_frame_t* frame) {
    uint32_t pid = frame->ebx;
    process_t* me = scheduler_current();

    process_t* child = scheduler_find_proc(pid);

    if (!child) {
        /* Child already cleaned up or never existed. */
        frame->eax = (uint32_t)-1;
        return;
    }

    if (child->state == PROC_ZOMBIE) {
        frame->eax = (uint32_t)child->exit_code;
        return;
    }

    /* Child still running — block until it exits.
       Critical section: setting waiter + state + yield must be atomic
       wrt the scheduler tick, otherwise scheduler_tick may try to
       kthread_switch us before we do it ourselves, corrupting kctx_esp. */
    __asm__ __volatile__("cli");
    child->waiter = me;
    me->state     = PROC_BLOCKED;
    me->wake_tick = 0;

    /* Yield to next ready thread (child will eventually run and wake us). */
    if (g_ready_head) {
        process_t* next = g_ready_head;
        g_ready_head = next->next;
        if (!g_ready_head) g_ready_tail = (process_t*)0;
        next->next = (process_t*)0;

        process_t* prev = me;
        next->state = PROC_RUNNING;
        g_current   = next;

        gdt_set_kernel_stack(next->kstack_top);
        msr_write(MSR_SYSENTER_ESP, next->kstack_top, 0);
        kthread_switch(&prev->kctx_esp, &next->kctx_esp);
    }
    /* We resume here when the child wakes us.  Interrupts are still
       disabled (scheduler_exit does cli before kthread_switch). */
    __asm__ __volatile__("sti");
    /* Collect exit code. */
    frame->eax = (uint32_t)child->exit_code;
}

static void _sys_open(isr_frame_t* frame) {
    const uint8_t* name = (const uint8_t*)frame->ebx;
    if (!name || !_user_range_ok(frame->ebx, 1)) { frame->eax = (uint32_t)-1; return; }

    vfs_file_t* f = vfs_lookup(name);
    if (!f) { frame->eax = (uint32_t)-1; return; }

    process_t* proc = scheduler_current();
    for (uint32_t i = 3; i < PROC_MAX_FD; i++) {
        if (!proc->fd_table[i].used) {
            proc->fd_table[i].file = f;
            proc->fd_table[i].pos  = 0;
            proc->fd_table[i].used = TRUE;
            frame->eax = i;
            return;
        }
    }
    frame->eax = (uint32_t)-1;
}

static void _sys_creat(isr_frame_t* frame) {
    const uint8_t* name = (const uint8_t*)frame->ebx;
    if (!name || !_user_range_ok(frame->ebx, 1)) { frame->eax = (uint32_t)-1; return; }

    vfs_file_t* f = vfs_creat(name);
    if (!f) { frame->eax = (uint32_t)-1; return; }

    process_t* proc = scheduler_current();
    for (uint32_t i = 3; i < PROC_MAX_FD; i++) {
        if (!proc->fd_table[i].used) {
            proc->fd_table[i].file = f;
            proc->fd_table[i].pos  = 0;
            proc->fd_table[i].used = TRUE;
            frame->eax = i;
            return;
        }
    }
    frame->eax = (uint32_t)-1;
}

static void _sys_pipe(isr_frame_t* frame) {
    /* EBX = pointer to user-space int fd[2] */
    if (!_user_range_ok(frame->ebx, 8)) { frame->eax = (uint32_t)-1; return; }

    pipe_t* p = pipe_alloc();
    if (!p) { frame->eax = (uint32_t)-1; return; }

    process_t* proc = scheduler_current();
    int32_t fd_read = -1, fd_write = -1;

    /* Find two free fd slots. */
    for (uint32_t i = 3; i < PROC_MAX_FD; i++) {
        if (!proc->fd_table[i].used) {
            if (fd_read < 0) {
                fd_read = (int32_t)i;
                proc->fd_table[i].type = FD_PIPE;
                proc->fd_table[i].pipe = p;
                proc->fd_table[i].pos  = 0;
                proc->fd_table[i].used = TRUE;
            } else if (fd_write < 0) {
                fd_write = (int32_t)i;
                proc->fd_table[i].type = FD_PIPE;
                proc->fd_table[i].pipe = p;
                proc->fd_table[i].pos  = 0;
                proc->fd_table[i].used = TRUE;
                break;
            }
        }
    }

    if (fd_write < 0) {
        /* Not enough fd slots — unwind. */
        if (fd_read >= 0) {
            proc->fd_table[fd_read].used = FALSE;
            proc->fd_table[fd_read].type = FD_FILE;
            proc->fd_table[fd_read].file = (void*)0;
        }
        pipe_close(p);  /* frees the pipe */
        frame->eax = (uint32_t)-1;
        return;
    }

    /* Write fds to user space. */
    uint32_t* user_fds = (uint32_t*)frame->ebx;
    user_fds[0] = (uint32_t)fd_read;
    user_fds[1] = (uint32_t)fd_write;
    frame->eax = 0;
}

static void _sys_dup(isr_frame_t* frame) {
    uint32_t oldfd = frame->ebx;
    process_t* proc = scheduler_current();

    if (oldfd >= PROC_MAX_FD || !proc->fd_table[oldfd].used) {
        frame->eax = (uint32_t)-1;
        return;
    }

    for (uint32_t i = 3; i < PROC_MAX_FD; i++) {
        if (!proc->fd_table[i].used) {
            proc->fd_table[i].type = proc->fd_table[oldfd].type;
            proc->fd_table[i].file = proc->fd_table[oldfd].file;
            proc->fd_table[i].pos  = 0;
            proc->fd_table[i].used = TRUE;
            frame->eax = i;
            return;
        }
    }
    frame->eax = (uint32_t)-1;
}

static void _sys_close(isr_frame_t* frame) {
    uint32_t fd = frame->ebx;
    if (fd >= PROC_MAX_FD) { frame->eax = (uint32_t)-1; return; }

    process_t* proc = scheduler_current();
    if (!proc->fd_table[fd].used) { frame->eax = (uint32_t)-1; return; }

    if (proc->fd_table[fd].type == FD_PIPE && proc->fd_table[fd].pipe) {
        pipe_close(proc->fd_table[fd].pipe);
    }

    proc->fd_table[fd].type = FD_FILE;
    proc->fd_table[fd].used = FALSE;
    proc->fd_table[fd].file = (void*)0;
    proc->fd_table[fd].pos  = 0;
    frame->eax = 0;
}

static void _sys_sigaction(isr_frame_t* frame) {
    uint32_t     signum = frame->ebx;
    sigaction_t* act    = (sigaction_t*)frame->esi;

    if (signum >= NSIG || signum == 0) { frame->eax = (uint32_t)-1; return; }
    if (signum == SIGKILL || signum == SIGSTOP) { frame->eax = (uint32_t)-1; return; }

    if (act) {
        if (!_user_range_ok(frame->esi, sizeof(sigaction_t))) { frame->eax = (uint32_t)-1; return; }
        process_t* proc = scheduler_current();
        proc->sigactions[signum].handler = act->handler;
        proc->sigactions[signum].flags   = act->flags;
    }
    frame->eax = 0;
}

static void _sys_kill(isr_frame_t* frame) {
    uint32_t pid = frame->ebx;
    uint32_t sig = frame->esi;

    if (sig >= NSIG || sig == 0) { frame->eax = (uint32_t)-1; return; }

    process_t* target = scheduler_find_proc(pid);
    if (!target) { frame->eax = (uint32_t)-1; return; }

    if (sig == SIGKILL && target->sigactions[SIGKILL].handler != SIG_DFL) {
        target->sigactions[SIGKILL].handler = SIG_DFL;
    }

    target->sig_pending |= (1u << sig);
    frame->eax = 0;
}

static void _sys_getpid(isr_frame_t* frame) {
    frame->eax = scheduler_current()->pid;
}

static void _sys_ioctl(isr_frame_t* frame) {
    uint32_t fd  = frame->ebx;
    uint32_t req = frame->esi;
    void*    arg = (void*)frame->edi;

    if (fd == STDIN_FD) {
        if (arg && !_user_range_ok(frame->edi, sizeof(termios_t))) { frame->eax = (uint32_t)-1; return; }
        frame->eax = (uint32_t)tty_ioctl(req, arg);
        return;
    }
    frame->eax = (uint32_t)-1;
}

static void _sys_mkdir(isr_frame_t* frame) {
    const uint8_t* path = (const uint8_t*)frame->ebx;
    if (!path || !_user_range_ok(frame->ebx, 1)) { frame->eax = (uint32_t)-1; return; }

    process_t* proc = scheduler_current();
    uint32_t base = (path[0] == '/') ? noxfs_root_ino() : proc->cwd_ino;
    uint32_t ino  = noxfs_mkdir(base, path);
    frame->eax = (ino != (uint32_t)-1) ? 0 : (uint32_t)-1;
}

static void _sys_chdir(isr_frame_t* frame) {
    const uint8_t* path = (const uint8_t*)frame->ebx;
    if (!path || !_user_range_ok(frame->ebx, 1)) { frame->eax = (uint32_t)-1; return; }

    process_t* proc = scheduler_current();
    uint32_t base = (path[0] == '/') ? noxfs_root_ino() : proc->cwd_ino;
    uint32_t ino  = noxfs_resolve(base, path);
    if (ino == (uint32_t)-1) { frame->eax = (uint32_t)-1; return; }

    proc->cwd_ino = ino;
    frame->eax = 0;
}

static void _sys_getdents(isr_frame_t* frame) {
    uint32_t fd     = frame->ebx;
    uint8_t* buf    = (uint8_t*)frame->esi;
    uint32_t len    = frame->edi;
    uint32_t* off_p = (uint32_t*)frame->edx; /* user-space offset pointer */

    if (!buf || len == 0 || !off_p) { frame->eax = (uint32_t)-1; return; }
    if (!_user_range_ok(frame->esi, len)) { frame->eax = (uint32_t)-1; return; }
    if (!_user_range_ok(frame->edx, 4)) { frame->eax = (uint32_t)-1; return; }

    process_t* proc = scheduler_current();
    uint32_t dir_ino = proc->cwd_ino;

    if (fd < PROC_MAX_FD && proc->fd_table[fd].used) {
        vfs_file_t* f = proc->fd_table[fd].file;
        if (f && f->inode) dir_ino = f->inode;
    }

    uint32_t off = *off_p;
    int32_t n = noxfs_getdents(dir_ino, buf, len, &off);
    if (n > 0) *off_p = off;
    frame->eax = n >= 0 ? (uint32_t)n : (uint32_t)-1;
}

static void _sys_stat(isr_frame_t* frame) {
    const uint8_t* path = (const uint8_t*)frame->ebx;
    vfs_file_t*    sb   = (vfs_file_t*)frame->esi;
    if (!path || !sb) { frame->eax = (uint32_t)-1; return; }
    if (!_user_range_ok(frame->ebx, 1)) { frame->eax = (uint32_t)-1; return; }
    if (!_user_range_ok(frame->esi, sizeof(vfs_file_t))) { frame->eax = (uint32_t)-1; return; }

    process_t* proc = scheduler_current();
    uint32_t base = (path[0] == '/') ? noxfs_root_ino() : proc->cwd_ino;
    uint32_t ino  = noxfs_resolve(base, path);
    if (ino == (uint32_t)-1) { frame->eax = (uint32_t)-1; return; }

    vfs_file_t st;
    if (noxfs_stat(ino, &st) != OS_OK) { frame->eax = (uint32_t)-1; return; }

    sb->size     = st.size;
    sb->inode    = st.inode;
    sb->capacity = st.capacity;
    frame->eax = 0;
}

static void _sys_lseek(isr_frame_t* frame) {
    uint32_t fd     = frame->ebx;
    int32_t  offset = (int32_t)frame->esi;
    uint32_t whence = frame->edi;

    process_t* proc = scheduler_current();
    if (fd >= PROC_MAX_FD || !proc->fd_table[fd].used) { frame->eax = (uint32_t)-1; return; }

    switch (whence) {
    case 0: /* SEEK_SET */
        if (offset < 0) { frame->eax = (uint32_t)-1; return; }
        proc->fd_table[fd].pos = (uint32_t)offset;
        break;
    case 1: /* SEEK_CUR */
        if (offset < 0 && (uint32_t)(-offset) > proc->fd_table[fd].pos)
            { frame->eax = (uint32_t)-1; return; }
        proc->fd_table[fd].pos = (uint32_t)((int32_t)proc->fd_table[fd].pos + offset);
        break;
    case 2: /* SEEK_END */ {
        vfs_file_t* f = proc->fd_table[fd].file;
        uint32_t end = f ? f->size : 0;
        if (offset < 0 && (uint32_t)(-offset) > end) { frame->eax = (uint32_t)-1; return; }
        proc->fd_table[fd].pos = (uint32_t)((int32_t)end + offset);
        break;
    }
    default:
        frame->eax = (uint32_t)-1; return;
    }
    frame->eax = proc->fd_table[fd].pos;
}

/* ── signal delivery ────────────────────────────────────────── */

void signal_deliver(isr_frame_t* frame) {
    process_t* cur = scheduler_current();
    if (!cur || cur->page_dir_phys == 0) return;

    uint32_t pending = cur->sig_pending & ~cur->sig_blocked;
    if (!pending) return;

    for (uint32_t sig = 1; sig < NSIG; sig++) {
        if (!(pending & (1u << sig))) continue;
        cur->sig_pending &= ~(1u << sig);

        sighandler_t handler = cur->sigactions[sig].handler;

        if (handler == SIG_DFL) {
            switch (sig) {
            case SIGKILL:
            case SIGTERM:
            case SIGINT:
            case SIGQUIT:
            case SIGSEGV:
            case SIGILL:
            case SIGFPE:
            case SIGBUS:
            case SIGABRT:
                cur->exit_code = 128 + sig;

                if (cur->is_fork_child) {
                    __asm__ __volatile__("cli");
                    cur->state = PROC_ZOMBIE;
                    if (cur->ppid) {
                        process_t* parent = scheduler_find_proc(cur->ppid);
                        if (parent)
                            parent->sig_pending |= (1u << SIGCHLD);
                    }
                    if (cur->waiter) {
                        scheduler_add(cur->waiter);
                        cur->waiter = (process_t*)0;
                    }
                    vfs_sync();
                    scheduler_exit();
                } else {
                    vfs_sync();
                    exec_return((int)cur->exit_code);
                }
                break;
            default:
                break;
            }
            return;
        }

        if (handler == SIG_IGN) continue;

        uint32_t uesp = frame->user_esp;
        if (uesp < 0x400008u || uesp > 0xC0000000u) return;

        uesp -= 8;
        uint32_t* ustack = (uint32_t*)uesp;
        ustack[0] = frame->eip;    /* return addr (popped by ret) */
        ustack[1] = sig;           /* argument to handler (at [esp+4]) */

        frame->user_esp = uesp;
        frame->eip       = (uint32_t)handler;
        return;
    }
}

/* Line-mode read. Routes through the fd table: fd=0 is keyboard (stdin),
   other fds are opened files. */
static void _sys_read(isr_frame_t* frame) {
    uint32_t fd      = frame->ebx;
    uint8_t* buf     = (uint8_t*)frame->esi;
    uint32_t maxlen  = frame->edi;

    if (maxlen == 0 || !buf) { frame->eax = (uint32_t)-1; return; }
    if (!_user_range_ok(frame->esi, maxlen)) { frame->eax = (uint32_t)-1; return; }

    process_t* proc = scheduler_current();

    /* Pipe-backed fd. */
    if (fd < PROC_MAX_FD && proc->fd_table[fd].used &&
        proc->fd_table[fd].type == FD_PIPE) {
        int32_t n = pipe_read(proc->fd_table[fd].pipe,
                              buf, maxlen);
        frame->eax = n >= 0 ? (uint32_t)n : (uint32_t)-1;
        return;
    }

    /* File-backed fd: read from open file. */
    if (fd < PROC_MAX_FD && proc->fd_table[fd].used) {
        vfs_file_t* f = proc->fd_table[fd].file;
        uint32_t pos = proc->fd_table[fd].pos;
        uint32_t remaining = f->size - pos;
        uint32_t to_copy = remaining < maxlen ? remaining : maxlen;

        for (uint32_t i = 0; i < to_copy; i++)
            buf[i] = f->data[pos + i];

        proc->fd_table[fd].pos += to_copy;
        frame->eax = to_copy;
        return;
    }

    /* stdin (keyboard) — routed through the TTY layer. */
    if (fd == STDIN_FD) {
        int32_t n = tty_read(buf, maxlen);
        frame->eax = n >= 0 ? (uint32_t)n : (uint32_t)-1;
        return;
    }

    frame->eax = (uint32_t)-1;
}

/* ── execve: replace the current process image ───────────────────
   Tears down the calling process's user address space and loads a new
   ELF in its place, then enters ring 3 at the new entry point.  Designed
   to be called from a fork child (the classic fork+execve shell pattern):
   the child has its own kernel stack + page directory, so freeing user
   space and re-entering ring 3 is self-contained.

   Failure model:
     - Bad path / not found  → return -1 (old image still intact).
     - Failure AFTER teardown → proc_terminate(127): the old image is gone,
       so there is nothing to return to. */
static void _sys_execve(isr_frame_t* frame) {
    const uint8_t* upath = (const uint8_t*)frame->ebx;
    if (!upath || !_user_range_ok(frame->ebx, 1)) {
        frame->eax = (uint32_t)-1; return;
    }

    /* Copy the program name into a kernel buffer BEFORE tearing down user
       space — upath points into memory we are about to free. */
    uint8_t name[64];
    uint32_t nlen = 0;
    for (; nlen < sizeof(name) - 1 && upath[nlen]; nlen++) name[nlen] = upath[nlen];
    name[nlen] = 0;

    vfs_file_t* f = vfs_lookup(name);
    if (!f) { frame->eax = (uint32_t)-1; return; }   /* recoverable */

    /* ── Point of no return: free PDEs 1..767 (pure user space).
       The recursive slot (1023) and the kernel higher-half (768+, which
       includes our own kernel stack) are left untouched. */
    process_t* proc = scheduler_current();
    uint32_t* pd = (uint32_t*)0xFFFFF000u;
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

    /* Load the new image into the now-empty user address space. */
    uint32_t entry, prog_end;
    if (elf_load(f->data, f->size, &entry, &prog_end) != OS_OK)
        proc_terminate(127);   /* old image gone — cannot recover */

    /* Reset the heap to just past the new image. */
    proc->brk_start = prog_end;
    proc->brk       = prog_end;

    /* Fresh user stack: only the top page is mapped; the rest grows on
       demand through the page-fault handler. */
    uint32_t stack_phys;
    if (pmm_alloc_frame(&stack_phys) != OS_OK ||
        vmm_map_page(USER_STACK_INIT, stack_phys,
                     PAGE_PRESENT | PAGE_RW | PAGE_USER) != OS_OK)
        proc_terminate(127);

    /* Build argv = { name, NULL } on the new stack.  The name string is
       copied here (it survives the teardown).  Frame layout matches
       exec_run: [esp]=argc, [esp+4]=argv[0], ... */
    uint32_t sp = USER_STACK_TOP;
    sp -= (nlen + 1);
    for (uint32_t k = 0; k <= nlen; k++) ((uint8_t*)sp)[k] = name[k];
    uint32_t arg0 = sp;
    sp &= ~3u;
    sp -= 4; *(uint32_t*)sp = 0;        /* argv[1] = NULL */
    sp -= 4; *(uint32_t*)sp = arg0;     /* argv[0]        */
    sp -= 4; *(uint32_t*)sp = 1;        /* argc           */

    /* Point sysenter at our kernel stack and drop to ring 3. */
    gdt_set_kernel_stack(proc->kstack_top);
    msr_write(MSR_SYSENTER_ESP, proc->kstack_top, 0);
    user_enter(entry, sp);   /* does not return */
}

/* ── brk: set/query the program break (user heap top) ────────────
   EBX = requested new break (0 = query only).  Returns the resulting
   break in EAX.  Pages between brk_start and brk fault in on demand via
   the page-fault handler, so this call only moves the boundary — it does
   not map anything itself.  Growth is capped at USER_HEAP_MAX and must
   not collide with the stack region. */
static void _sys_brk(isr_frame_t* frame) {
    process_t* me = scheduler_current();
    uint32_t req = frame->ebx;

    if (req == 0) { frame->eax = me->brk; return; }          /* query */

    /* Reject anything below the heap floor or above the ceiling. */
    if (req < me->brk_start || req > USER_HEAP_MAX) {
        frame->eax = me->brk;                                /* unchanged */
        return;
    }

    /* Grow or shrink the break.  Newly-exposed pages fault in on demand;
       pages above a shrunk break stay mapped until the process exits
       (vmm_destroy_pd frees them) — simple and leak-free across exec. */
    me->brk = req;
    frame->eax = me->brk;
}

static void _syscall_dispatch(isr_frame_t* frame) {
    switch (frame->eax) {
    case SYS_WRITE:     _sys_write(frame);    break;
    case SYS_READ:      _sys_read(frame);     break;
    case SYS_EXIT:      _sys_exit(frame);     break;
    case SYS_OPEN:      _sys_open(frame);     break;
    case SYS_CLOSE:     _sys_close(frame);    break;
    case SYS_FORK:      _sys_fork(frame);     break;
    case SYS_WAITPID:   _sys_waitpid(frame);  break;
    case SYS_CREAT:     _sys_creat(frame);    break;
    case SYS_PIPE:      _sys_pipe(frame);     break;
    case SYS_DUP:       _sys_dup(frame);      break;
    case SYS_SIGACTION: _sys_sigaction(frame); break;
    case SYS_KILL:      _sys_kill(frame);     break;
    case SYS_GETPID:    _sys_getpid(frame);   break;
    case SYS_IOCTL:     _sys_ioctl(frame);    break;
    case SYS_MKDIR:     _sys_mkdir(frame);    break;
    case SYS_CHDIR:     _sys_chdir(frame);    break;
    case SYS_GETDENTS:  _sys_getdents(frame); break;
    case SYS_STAT:      _sys_stat(frame);     break;
    case SYS_LSEEK:     _sys_lseek(frame);    break;
    case SYS_EXECVE:    _sys_execve(frame);   break;
    case SYS_BRK:       _sys_brk(frame);      break;
    default: break;
    }
}

void syscall_handler(isr_frame_t* frame) {
    _syscall_dispatch(frame);
    signal_deliver(frame);
}

os_status_t syscall_init(void) {
    isr_register_handler(128, syscall_handler);
    idt_set_gate(128, (uint32_t)isr_stub_128,
                 IDT_PRESENT | IDT_DPL3 | IDT_GATE_INT32);
    msr_write(MSR_SYSENTER_CS,  0x08, 0);
    msr_write(MSR_SYSENTER_ESP, scheduler_current()->kstack_top, 0);
    msr_write(MSR_SYSENTER_EIP, (uint32_t)sysenter_entry, 0);
    return OS_OK;
}
