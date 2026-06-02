/**
 * @file    mm/virt/pagefault.c
 * @brief   Page-fault handler (#PF, vector 14) with demand paging.
 *
 *  Recoverable faults:
 *    - A not-present access from ring 3 inside the user-stack region
 *      grows the stack by one zero-filled page (lazy stack allocation).
 *
 *  Fatal faults:
 *    - Any other ring-3 fault (unmapped address, protection violation)
 *      terminates the offending process like a SIGSEGV — the kernel
 *      survives and control returns to the shell.
 *    - A ring-0 (kernel) fault is unrecoverable → kernel panic.
 *
 * @author  Noxis Team
 * @date    2026-05-30
 */
#include <kernel/isr/isr.h>
#include <kernel/core/panic.h>
#include <mm/virt/vmm.h>
#include <mm/virt/paging.h>
#include <mm/virt/uvm.h>
#include <mm/phys/pmm.h>
#include <proc/scheduler.h>
#include <drivers/vga.h>
#include <common/types.h>

/* Forward declaration — defined in sys_signal.c */
struct isr_frame;
void signal_deliver(isr_frame_t* frame);

/* Kernel-stack address window (see proc_spawn: 0xD0000000 + pid*0x10000). */
#define KSTACK_REGION_BASE  0xD0000000u
#define KSTACK_REGION_TOP   0xE0000000u

/* Page-fault error-code bits (pushed by the CPU). */
#define PF_PRESENT  0x1   /* 0 = not-present page, 1 = protection violation */
#define PF_WRITE    0x2   /* 0 = read, 1 = write                            */
#define PF_USER     0x4   /* 0 = supervisor, 1 = user-mode access           */

static uint32_t _read_cr2(void) {
    uint32_t v;
    __asm__ __volatile__("mov %%cr2, %0" : "=r"(v));
    return v;
}

static void _puthex(uint32_t v) {
    static const uint8_t hx[] = "0123456789ABCDEF";
    uint8_t buf[11];
    buf[0] = '0'; buf[1] = 'x';
    for (uint32_t i = 0; i < 8; i++)
        buf[2 + i] = hx[(v >> ((7 - i) * 4)) & 0xF];
    buf[10] = 0;
    vga_write(buf);
}

static void _report_user_fault(uint32_t cr2, uint32_t err, isr_frame_t* frame) {
    vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
    vga_write((const uint8_t*)"\n  segfault: process killed (page fault)\n");
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_write((const uint8_t*)"    addr="); _puthex(cr2);
    vga_write((const uint8_t*)" eip=");     _puthex(frame->eip);
    vga_write((const uint8_t*)" err=");     _puthex(err);
    vga_put_char('\n');
    vga_write((const uint8_t*)"    eax=");  _puthex(frame->eax);
    vga_write((const uint8_t*)" ebx=");     _puthex(frame->ebx);
    vga_write((const uint8_t*)" ecx=");     _puthex(frame->ecx);
    vga_write((const uint8_t*)" edx=");     _puthex(frame->edx);
    vga_put_char('\n');
    vga_write((const uint8_t*)"    esp=");  _puthex(frame->user_esp);
    vga_write((const uint8_t*)" ebp=");     _puthex(frame->ebp);
    vga_put_char('\n');
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

static void _pagefault_handler(isr_frame_t* frame) {
    uint32_t cr2 = _read_cr2();
    uint32_t err = frame->error_code;
    int present   = err & PF_PRESENT;
    int from_user = (frame->cs & 3) == 3;

    process_t* me = scheduler_current();

    /* ── Copy-on-write ─────────────────────────────────────────
       A write to a present page that carries the PAGE_COW bit means
       a fork-shared page is being modified.  vmm_handle_cow gives
       this process a private, writable copy and we retry.          */
    if (present && (err & PF_WRITE)) {
        if (vmm_handle_cow(cr2))
            return;   /* CPU re-executes the faulting write */
    }

    /* ── Demand paging ─────────────────────────────────────────
       A not-present access inside either the stack region (grows down)
       or the heap region [brk_start, brk) maps a fresh zero-filled page
       and retries the faulting instruction.

       This must also fire for faults taken in KERNEL mode: during a
       syscall the kernel legitimately reads/writes a user buffer that
       lives on a not-yet-paged user stack page (e.g. getdents writing
       into a large stack buffer in the shell).  Gating on `from_user`
       would wrongly panic.  A genuine kernel null-deref is outside the
       stack/heap regions, so it still falls through to the panic. */
    (void)from_user;
    int in_stack = (cr2 >= USER_STACK_LIMIT && cr2 < USER_STACK_TOP);
    int in_heap  = (me && me->brk_start && cr2 >= me->brk_start && cr2 < me->brk);
    if (!present && (in_stack || in_heap)) {
        uint32_t va = PAGE_ALIGN_DOWN(cr2);
        uint32_t phys;
        if (pmm_alloc_frame(&phys) == OS_OK &&
            vmm_map_page(va, phys,
                         PAGE_PRESENT | PAGE_RW | PAGE_USER) == OS_OK) {
            uint8_t* p = (uint8_t*)va;
            for (uint32_t i = 0; i < PAGE_SIZE; i++) p[i] = 0;
            return;  /* CPU re-executes the faulting instruction */
        }
        /* Out of memory — fall through and kill the process. */
    }

    /* ── Fatal user fault: deliver SIGSEGV ──────────────────────── */
    if (from_user) {
        _report_user_fault(cr2, err, frame);
        if (me) {
            me->sig_pending |= (1u << SIGSEGV);
            signal_deliver(frame);   /* delivers immediately; kills if SIG_DFL */
        }
        /* If somehow we return (shouldn't happen), force kill. */
        proc_terminate(0x8B);
    }

    /* ── Kernel stack overflow detection (guard page) ───────────
       Per-process kernel stacks live at 0xD0000000 + pid*0x10000,
       PROC_KSTACK_PAGES mapped, with the rest of each 64 KB slot left
       unmapped — an implicit guard region.  A kernel-mode fault landing
       in this window is almost always a kernel stack overflow, which is
       far more useful to name than a generic page fault. */
    if (cr2 >= KSTACK_REGION_BASE && cr2 < KSTACK_REGION_TOP) {
        kernel_panic((const uint8_t*)"KERNEL STACK OVERFLOW (guard page hit)", frame);
    }

    /* ── Fatal kernel fault: unrecoverable ─────────────────────── */
    kernel_panic((const uint8_t*)"Page Fault (kernel mode)", frame);
}

void pagefault_init(void) {
    isr_register_handler(14, _pagefault_handler);
}
