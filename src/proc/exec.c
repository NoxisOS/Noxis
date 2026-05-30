/**
 * @file    proc/exec.c
 * @brief   Runs an ELF in ring 3 inside a dedicated per-process address space.
 *
 *  - A fresh page directory is created (kernel PDEs cloned, user space empty).
 *  - ELF segments + user stack are mapped into that PD.
 *  - CR3 is switched to the per-process PD before iret.
 *  - sys_exit → exec_return → longjmp back here, then CR3 restored + PD freed.
 *
 * @author  Noxis Team
 * @date    2026-05-30
 */
#include <proc/exec.h>
#include <proc/elf.h>
#include <proc/scheduler.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <mm/paging.h>
#include <hal/gdt.h>
#include <common/types.h>

extern void user_enter(uint32_t entry, uint32_t stack);
extern int  kjmp_save(uint32_t* buf);
extern void kjmp_restore(uint32_t* buf, int val) __attribute__((noreturn));
extern void msr_write(uint32_t msr, uint32_t lo, uint32_t hi);

#define MSR_SYSENTER_ESP  0x175u
#define KERNEL_PD_PHYS    0x400000u

#define USER_STACK_VIRT  0xB0000000u
#define USER_STACK_TOP   (USER_STACK_VIRT + PAGE_SIZE)
#define JMPBUF_DWORDS    7
#define MAX_ARGV         16

static uint32_t g_jmpbuf[JMPBUF_DWORDS];
static int      g_active;
static int      g_exit_code;
static uint32_t g_exec_pd;   /* per-exec PD; freed on exec_return */

static uint32_t _strlen(const uint8_t* s) {
    uint32_t n = 0; while (s[n]) n++; return n;
}

static uint32_t _build_argv_frame(uint32_t argc, const uint8_t* const* argv) {
    uint32_t sp = USER_STACK_TOP;

    uint32_t ptrs[MAX_ARGV];
    if (argc > MAX_ARGV) argc = MAX_ARGV;
    for (int32_t i = (int32_t)argc - 1; i >= 0; i--) {
        uint32_t n = _strlen(argv[i]) + 1;
        sp -= n;
        uint8_t* dst = (uint8_t*)sp;
        for (uint32_t j = 0; j < n; j++) dst[j] = argv[i][j];
        ptrs[i] = sp;
    }
    sp &= ~3u;
    sp -= 4; *(uint32_t*)sp = 0;
    for (int32_t i = (int32_t)argc - 1; i >= 0; i--) {
        sp -= 4; *(uint32_t*)sp = ptrs[i];
    }
    sp -= 4; *(uint32_t*)sp = argc;
    return sp;
}

os_status_t exec_run(const uint8_t* elf, uint32_t size,
                     uint32_t argc, const uint8_t* const* argv,
                     int* exit_code_out) {
    /* ── 1. Create a fresh per-exec address space ──────────── */
    uint32_t pd_phys;
    if (vmm_create_pd(&pd_phys) != OS_OK) return OS_ERR_OOM;

    /* Switch to the new PD so elf_load's vmm_map_page writes into it. */
    vmm_switch_pd(pd_phys);
    g_exec_pd = pd_phys;
    scheduler_current()->page_dir_phys = pd_phys;

    /* ── 2. Load ELF (maps pages into current = per-exec PD) ── */
    uint32_t entry;
    os_status_t s = elf_load(elf, size, &entry);
    if (s != OS_OK) {
        vmm_switch_pd(KERNEL_PD_PHYS);
        vmm_destroy_pd(pd_phys);
        scheduler_current()->page_dir_phys = 0;
        return s;
    }

    /* ── 3. Map user stack ─────────────────────────────────── */
    uint32_t stack_phys;
    if (pmm_alloc_frame(&stack_phys) != OS_OK) {
        vmm_switch_pd(KERNEL_PD_PHYS);
        vmm_destroy_pd(pd_phys);
        scheduler_current()->page_dir_phys = 0;
        return OS_ERR_OOM;
    }
    if (vmm_map_page(USER_STACK_VIRT, stack_phys,
                     PAGE_PRESENT | PAGE_RW | PAGE_USER) != OS_OK) {
        vmm_switch_pd(KERNEL_PD_PHYS);
        vmm_destroy_pd(pd_phys);
        scheduler_current()->page_dir_phys = 0;
        return OS_ERR_IO;
    }

    /* ── 4. Build argv on user stack ───────────────────────── */
    uint32_t user_esp = _build_argv_frame(argc, argv);

    /* ── 5. Set TSS + SYSENTER_ESP to our kernel stack ──────── */
    process_t* me = scheduler_current();
    gdt_set_kernel_stack(me->kstack_top);
    msr_write(MSR_SYSENTER_ESP, me->kstack_top, 0);

    /* ── 6. Jump to ring 3 (longjmp escape hatch for sys_exit) ─ */
    g_active = 1;
    if (kjmp_save(g_jmpbuf) == 0) {
        user_enter(entry, user_esp);   /* does not return normally */
    }
    /* sys_exit → exec_return → kjmp_restore lands here. */
    g_active = 0;
    if (exit_code_out) *exit_code_out = g_exit_code;

    /* ── 7. Restore kernel address space + free per-exec PD ── */
    vmm_switch_pd(KERNEL_PD_PHYS);
    vmm_destroy_pd(pd_phys);
    scheduler_current()->page_dir_phys = 0;
    g_exec_pd = 0;

    return OS_OK;
}

void exec_return(int code) {
    g_exit_code = code;
    if (!g_active) {
        for (;;) __asm__ __volatile__("hlt");
    }
    kjmp_restore(g_jmpbuf, 1);
}

uint32_t exec_current_pd(void) { return g_exec_pd; }
