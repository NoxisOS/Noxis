/**
 * @file    kernel/hal/fpu.c
 * @brief   x87 FPU bring-up + lazy state switching (Linux-0.01 style).
 *
 *  The FPU register file is saved/restored lazily.  CR0.TS is set on every
 *  context switch; the first FPU instruction a process executes then traps
 *  #NM (Device Not Available, vector 7).  The handler clears TS, FNSAVEs
 *  the previous owner's registers, FRSTORs (or FNINITs) the current
 *  process's, and records it as the new owner.  Processes that never touch
 *  the FPU pay nothing.
 *
 * @author  Noxis Team
 * @date    2026-05-30
 */
#include <kernel/hal/fpu.h>
#include <kernel/isr/isr.h>
#include <proc/scheduler.h>
#include <proc/process.h>
#include <common/types.h>

#define CR0_MP  (1u << 1)
#define CR0_EM  (1u << 2)
#define CR0_TS  (1u << 3)
#define CR0_NE  (1u << 5)

/* The process whose registers currently live in the FPU (NULL = none). */
static process_t* g_fpu_owner;

static inline uint32_t _read_cr0(void) {
    uint32_t v; __asm__ __volatile__("mov %%cr0, %0" : "=r"(v)); return v;
}
static inline void _write_cr0(uint32_t v) {
    __asm__ __volatile__("mov %0, %%cr0" : : "r"(v) : "memory");
}

/* #NM (vector 7): a process touched the FPU while CR0.TS was set. */
static void _nm_handler(isr_frame_t* frame) {
    (void)frame;
    __asm__ __volatile__("clts");          /* FPU usable again */

    process_t* cur = scheduler_current();
    if (g_fpu_owner == cur) return;        /* still ours — only TS needed */

    if (g_fpu_owner)
        __asm__ __volatile__("fnsave (%0)" : : "r"(g_fpu_owner->fpu_state)
                                            : "memory");

    if (cur->fpu_used) {
        __asm__ __volatile__("frstor (%0)" : : "r"(cur->fpu_state) : "memory");
    } else {
        __asm__ __volatile__("fninit");
        cur->fpu_used = TRUE;
    }
    g_fpu_owner = cur;
}

os_status_t fpu_init(void) {
    uint32_t cr0 = _read_cr0();
    cr0 &= ~CR0_EM;    /* use the real x87, not emulation */
    cr0 |=  CR0_MP;    /* monitor coprocessor (pairs with TS) */
    cr0 |=  CR0_NE;    /* native #MF reporting (not the legacy PIC IRQ13) */
    _write_cr0(cr0);

    __asm__ __volatile__("fninit");
    g_fpu_owner = (process_t*)0;

    isr_register_handler(7, _nm_handler);

    _write_cr0(_read_cr0() | CR0_TS);   /* arm lazy switching */
    return OS_OK;
}

void fpu_set_ts(void) {
    _write_cr0(_read_cr0() | CR0_TS);
}

void fpu_drop_owner(process_t* p) {
    if (g_fpu_owner == p) g_fpu_owner = (process_t*)0;
}
