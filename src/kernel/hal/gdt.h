/**
 * @file    hal/gdt.h
 * @brief   64-bit Global Descriptor Table + TSS.
 * @author  Noxis Team
 */
#ifndef HAL_GDT_H
#define HAL_GDT_H

#include <common/types.h>
#include <common/status.h>

/* Selectors (index << 3 | rpl). */
#define SEL_KCODE  0x08
#define SEL_KDATA  0x10
#define SEL_UCODE  0x1B   /* user code, rpl=3 */
#define SEL_UDATA  0x23   /* user data, rpl=3 */
#define SEL_TSS    0x28

/* Set the ring-0 stack pointer used on user→kernel transitions. */
void gdt_set_kernel_stack(uint64_t rsp);

/* Build and load the 64-bit GDT + TSS. */
os_status_t gdt_init(void);

#endif /* HAL_GDT_H */
