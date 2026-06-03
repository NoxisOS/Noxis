/**
 * @file    kernel/isr/isr.h
 * @brief   64-bit interrupt frame + ISR dispatcher.
 * @author  Noxis Team
 */
#ifndef KERNEL_ISR_H
#define KERNEL_ISR_H

#include <common/types.h>
#include <common/status.h>

#define ISR_MAX_HANDLERS  256

/* Interrupt frame. Field order matches the push order in isr_common:
   the C struct's first member sits at the lowest address (= RSP after
   all pushes), so registers are pushed rax-first … r15-last. */
typedef struct __attribute__((packed)) {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error_code;          /* pushed by stub */
    uint64_t rip, cs, rflags, rsp, ss;    /* pushed by CPU  */
} isr_frame_t;

typedef void (*isr_handler_t)(isr_frame_t* frame);

void        isr_init(void);
os_status_t isr_register_handler(uint8_t vector, isr_handler_t handler);
void        isr_handler(isr_frame_t* frame);   /* called from isr_common */

#endif /* KERNEL_ISR_H */
