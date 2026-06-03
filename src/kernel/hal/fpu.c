/**
 * @file    kernel/hal/fpu.c
 * @brief   x87 + SSE bring-up for long mode.
 *
 * x86-64 guarantees SSE, so we enable it unconditionally (userland may
 * use it).  Lazy per-process FPU state switching will be re-added when
 * the scheduler is ported.
 */
#include <kernel/hal/fpu.h>
#include <common/types.h>

#define CR0_MP         (1ULL << 1)
#define CR0_EM         (1ULL << 2)
#define CR0_NE         (1ULL << 5)
#define CR4_OSFXSR     (1ULL << 9)
#define CR4_OSXMMEXCPT (1ULL << 10)

static inline uint64_t rd_cr0(void){ uint64_t v; __asm__ __volatile__("mov %%cr0,%0":"=r"(v)); return v; }
static inline void     wr_cr0(uint64_t v){ __asm__ __volatile__("mov %0,%%cr0"::"r"(v):"memory"); }
static inline uint64_t rd_cr4(void){ uint64_t v; __asm__ __volatile__("mov %%cr4,%0":"=r"(v)); return v; }
static inline void     wr_cr4(uint64_t v){ __asm__ __volatile__("mov %0,%%cr4"::"r"(v):"memory"); }

os_status_t fpu_init(void) {
    uint64_t cr0 = rd_cr0();
    cr0 &= ~CR0_EM;     /* real x87, not emulation */
    cr0 |=  CR0_MP;
    cr0 |=  CR0_NE;
    wr_cr0(cr0);

    uint64_t cr4 = rd_cr4();
    cr4 |= CR4_OSFXSR | CR4_OSXMMEXCPT;   /* enable SSE + FXSAVE */
    wr_cr4(cr4);

    __asm__ __volatile__("fninit");
    return OS_OK;
}
