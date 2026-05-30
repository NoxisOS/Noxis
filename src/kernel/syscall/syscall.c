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
#include <fs/pipe/pipe.h>
#include <common/types.h>
#include <common/signal.h>
#include <mm/virt/vmm.h>

#define MSR_SYSENTER_CS   0x174
#define MSR_SYSENTER_ESP  0x175
#define MSR_SYSENTER_EIP  0x176

extern void isr_stub_128(void);
extern void sysenter_entry(void);
extern void msr_write(uint32_t msr, uint32_t low, uint32_t high);
extern void kthread_switch(uint32_t* old_esp, uint32_t* new_esp);
extern void gdt_set_kernel_stack(uint32_t esp);

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
    process_t* me = scheduler_current();
    int code = (int)frame->ebx;

    if (me->is_fork_child) {
        /* Block PIT ticks during the exit sequence: set exit_code + mark
           ZOMBIE + wake parent must be atomic relative to the scheduler.
           scheduler_exit() does its own cli, but the window between the
           stub's sti and ours is large enough for a tick to fire. */
        __asm__ __volatile__("cli");
        me->exit_code = code;
        me->state     = PROC_ZOMBIE;

        /* Generate SIGCHLD on the parent if it has not already been reaped. */
        if (me->ppid) {
            process_t* parent = scheduler_find_proc(me->ppid);
            if (parent)
                parent->sig_pending |= (1u << SIGCHLD);
        }

        if (me->waiter) {
            scheduler_add(me->waiter);
            me->waiter = (process_t*)0;
        }
        scheduler_exit(); /* does not return */
    } else {
        /* Close all file descriptors opened by this process before
           returning to the shell, so fd slots don't leak across execs. */
        process_t* proc = scheduler_current();
        for (uint32_t i = 3; i < PROC_MAX_FD; i++) {
            if (proc->fd_table[i].used) {
                if (proc->fd_table[i].type == FD_PIPE && proc->fd_table[i].pipe)
                    pipe_close(proc->fd_table[i].pipe);
                proc->fd_table[i].type = FD_FILE;
                proc->fd_table[i].used = FALSE;
                proc->fd_table[i].file = (vfs_file_t*)0;
                proc->fd_table[i].pos  = 0;
            }
        }
        vfs_sync();
        exec_return(code);
    }
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
