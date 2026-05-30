/**
 * @file    syscall/syscall.c
 * @brief   System call dispatcher (int 0x80 + sysenter).
 *          Convention: EAX=#, EBX=arg1, ESI=arg2, ECX=arg3.
 * @author  Noxis Team
 * @date    2026-05-30
 */
#include <syscall/syscall.h>
#include <kernel/isr.h>
#include <hal/idt.h>
#include <proc/scheduler.h>
#include <proc/process.h>
#include <proc/exec.h>
#include <drivers/vga.h>
#include <drivers/kbd.h>
#include <fs/vfs.h>
#include <common/types.h>
#include <mm/vmm.h>

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

static void _sys_write(isr_frame_t* frame) {
    const uint8_t* str = (const uint8_t*)frame->ebx;
    uint32_t len = frame->esi;
    if (len == 0) len = frame->ecx;

    /* Honor the user's length, but still stop at NUL — saves us from a runaway
       loop when the user passed a string-style buffer. */
    for (uint32_t i = 0; i < len; i++) {
        uint8_t c = str[i];
        if (c == 0) break;
        vga_put_char(c);
    }
    frame->eax = len;
}

static void _sys_exit(isr_frame_t* frame) {
    process_t* me = scheduler_current();
    int code = (int)frame->ebx;

    if (me->is_fork_child) {
        /* Fork child: mark ZOMBIE, wake waiting parent, then yield forever. */
        me->exit_code = code;
        me->state     = PROC_ZOMBIE;

        if (me->waiter) {
            scheduler_add(me->waiter);
            me->waiter = (process_t*)0;
        }
        scheduler_exit(); /* does not return */
    } else {
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
       We do NOT add to g_blocked_head: that list is for timer sleeps.
       The child's sys_exit calls scheduler_add(waiter) which adds us back
       to g_ready_head directly.  scheduler_tick's PROC_BLOCKED branch will
       switch away from us on the next tick if we don't kthread_switch here. */
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

        /* DO_SWITCH defined in scheduler.c — replicate inline here. */
        gdt_set_kernel_stack(next->kstack_top);
        msr_write(MSR_SYSENTER_ESP, next->kstack_top, 0);
        kthread_switch(&prev->kctx_esp, &next->kctx_esp);
        /* We resume here when the child wakes us. */
        __asm__ __volatile__("sti");
    }
    /* Collect exit code. */
    frame->eax = (uint32_t)child->exit_code;
}

static void _sys_open(isr_frame_t* frame) {
    const uint8_t* name = (const uint8_t*)frame->ebx;
    if (!name) { frame->eax = (uint32_t)-1; return; }

    const vfs_file_t* f = vfs_lookup(name);
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

static void _sys_close(isr_frame_t* frame) {
    uint32_t fd = frame->ebx;
    if (fd >= PROC_MAX_FD) { frame->eax = (uint32_t)-1; return; }

    process_t* proc = scheduler_current();
    if (!proc->fd_table[fd].used) { frame->eax = (uint32_t)-1; return; }

    proc->fd_table[fd].used = FALSE;
    proc->fd_table[fd].file = (void*)0;
    proc->fd_table[fd].pos  = 0;
    frame->eax = 0;
}

/* Line-mode read. Routes through the fd table: fd=0 is keyboard (stdin),
   other fds are opened files. */
static void _sys_read(isr_frame_t* frame) {
    uint32_t fd      = frame->ebx;
    uint8_t* buf     = (uint8_t*)frame->esi;
    uint32_t maxlen  = frame->edi;

    if (maxlen == 0 || !buf) { frame->eax = (uint32_t)-1; return; }

    process_t* proc = scheduler_current();

    /* File-backed fd: read from open file. */
    if (fd < PROC_MAX_FD && proc->fd_table[fd].used) {
        const vfs_file_t* f = proc->fd_table[fd].file;
        uint32_t pos = proc->fd_table[fd].pos;
        uint32_t remaining = f->size - pos;
        uint32_t to_copy = remaining < maxlen ? remaining : maxlen;

        for (uint32_t i = 0; i < to_copy; i++)
            buf[i] = f->data[pos + i];

        proc->fd_table[fd].pos += to_copy;
        frame->eax = to_copy;
        return;
    }

    /* stdin (keyboard) — keep original behaviour as fallback. */
    if (fd == STDIN_FD) {
        uint32_t len = 0;
        for (;;) {
            uint8_t c = kbd_getchar();
            if (c == '\b') {
                if (len > 0) { len--; vga_backspace(); }
                continue;
            }
            if (c == '\n' || c == '\r') {
                vga_put_char('\n');
                if (len < maxlen) buf[len++] = '\n';
                break;
            }
            if (c < ' ' || c >= 0x7F) continue;
            if (len + 1 >= maxlen) continue;
            buf[len++] = c;
            vga_put_char(c);
        }
        if (len < maxlen) buf[len] = 0;
        frame->eax = len;
        return;
    }

    frame->eax = (uint32_t)-1;
}

static void _syscall_dispatch(isr_frame_t* frame) {
    switch (frame->eax) {
    case SYS_WRITE:   _sys_write(frame);   break;
    case SYS_READ:    _sys_read(frame);    break;
    case SYS_EXIT:    _sys_exit(frame);    break;
    case SYS_OPEN:    _sys_open(frame);    break;
    case SYS_CLOSE:   _sys_close(frame);   break;
    case SYS_FORK:    _sys_fork(frame);    break;
    case SYS_WAITPID: _sys_waitpid(frame); break;
    default: break;
    }
}

void syscall_handler(isr_frame_t* frame) {
    _syscall_dispatch(frame);
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
