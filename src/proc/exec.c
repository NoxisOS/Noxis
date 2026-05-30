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
#define MAX_ARGV         16

static uint32_t g_jmpbuf[JMPBUF_DWORDS];
static int      g_active;
static int      g_exit_code;

static uint32_t _strlen(const uint8_t* s) {
    uint32_t n = 0; while (s[n]) n++; return n;
}

/* Build the argv frame at the top of the freshly mapped user stack and
   return the user ESP that should be loaded by iret. The kernel writes
   directly via the virtual address since the page is also mapped to the
   kernel address space (USER flag allows ring-3 access too, ring 0 always). */
static uint32_t _build_argv_frame(uint32_t argc, const uint8_t* const* argv) {
    uint32_t sp = USER_STACK_TOP;

    /* 1. push the argv strings (top of stack downward) */
    uint32_t ptrs[MAX_ARGV];
    if (argc > MAX_ARGV) argc = MAX_ARGV;
    for (int32_t i = (int32_t)argc - 1; i >= 0; i--) {
        uint32_t n = _strlen(argv[i]) + 1;
        sp -= n;
        uint8_t* dst = (uint8_t*)sp;
        for (uint32_t j = 0; j < n; j++) dst[j] = argv[i][j];
        ptrs[i] = sp;
    }

    /* 2. align sp to 4 bytes */
    sp &= ~3u;

    /* 3. NULL terminator for argv[] */
    sp -= 4;
    *(uint32_t*)sp = 0;

    /* 4. argv[argc-1] .. argv[0] (high-index pushed first) */
    for (int32_t i = (int32_t)argc - 1; i >= 0; i--) {
        sp -= 4;
        *(uint32_t*)sp = ptrs[i];
    }

    /* 5. argc */
    sp -= 4;
    *(uint32_t*)sp = argc;

    return sp;
}

os_status_t exec_run(const uint8_t* elf, uint32_t size,
                     uint32_t argc, const uint8_t* const* argv,
                     int* exit_code_out) {
    uint32_t entry;
    os_status_t s = elf_load(elf, size, &entry);
    if (s != OS_OK) return s;

    /* Map (or remap) one page of user stack. */
    uint32_t stack_phys;
    if (pmm_alloc_frame(&stack_phys) != OS_OK) return OS_ERR_OOM;
    if (vmm_map_page(USER_STACK_VIRT, stack_phys,
                     PAGE_PRESENT | PAGE_RW | PAGE_USER) != OS_OK) {
        return OS_ERR_IO;
    }

    uint32_t user_esp = _build_argv_frame(argc, argv);

    /* IRQs that fire while ring 3 is running land here. */
    gdt_set_kernel_stack(scheduler_current()->kstack_top);

    g_active = 1;
    if (kjmp_save(g_jmpbuf) == 0) {
        user_enter(entry, user_esp);
    }
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
