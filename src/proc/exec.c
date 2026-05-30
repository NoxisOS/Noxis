/**
 * @file    proc/exec.c
 * @brief   Runs an ELF in ring 3. The "return to shell" plumbing uses
 *          kjmp_save/kjmp_restore so sys_exit can abandon the user mode
 *          stack and resume the kernel inside exec_run().
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

#define USER_STACK_VIRT  0xB0000000u
#define USER_STACK_TOP   (USER_STACK_VIRT + PAGE_SIZE)
#define JMPBUF_DWORDS    7

static uint32_t g_jmpbuf[JMPBUF_DWORDS];
static int      g_active;
static int      g_exit_code;

os_status_t exec_run(const uint8_t* elf, uint32_t size, int* exit_code_out) {
    uint32_t entry;
    os_status_t s = elf_load(elf, size, &entry);
    if (s != OS_OK) return s;

    /* Map (or remap) one page of user stack. vmm_map_page overwrites
       the PTE if it was already present, so consecutive execs are fine. */
    uint32_t stack_phys;
    if (pmm_alloc_frame(&stack_phys) != OS_OK) return OS_ERR_OOM;
    if (vmm_map_page(USER_STACK_VIRT, stack_phys,
                     PAGE_PRESENT | PAGE_RW | PAGE_USER) != OS_OK) {
        return OS_ERR_IO;
    }

    /* IRQs that fire while ring 3 is running land here. */
    gdt_set_kernel_stack(scheduler_current()->kstack_top);

    g_active = 1;
    if (kjmp_save(g_jmpbuf) == 0) {
        /* Direct call: jump to user mode. Control never falls through. */
        user_enter(entry, USER_STACK_TOP);
    }
    /* Reached via exec_return → kjmp_restore */
    g_active = 0;
    if (exit_code_out) *exit_code_out = g_exit_code;
    return OS_OK;
}

void exec_return(int code) {
    g_exit_code = code;
    if (!g_active) {
        /* No active exec — shouldn't happen, but halt rather than crash. */
        for (;;) __asm__ __volatile__("hlt");
    }
    kjmp_restore(g_jmpbuf, 1);
}
