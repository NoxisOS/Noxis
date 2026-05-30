/**
 * @file    hal/idt.c
 * @brief   Interrupt Descriptor Table — set up gates, load IDT
 * @author  Noxis Team
 * @date    2026-05-29
 */
#include <kernel/hal/idt.h>
#include <common/types.h>

/* ── file-scope state ──────────────────────────────────────── */
static idt_entry_t g_idt[IDT_ENTRIES];
static idt_ptr_t   g_idt_ptr;

/* ── external ASM ──────────────────────────────────────────── */
extern void idt_flush(idt_ptr_t* ptr);

/* ── Default handler — defined in isr_stubs.asm ────────────── */
extern void isr_stub_0(void);
extern void isr_stub_1(void);
extern void isr_stub_2(void);
extern void isr_stub_3(void);
extern void isr_stub_4(void);
extern void isr_stub_5(void);
extern void isr_stub_6(void);
extern void isr_stub_7(void);
extern void isr_stub_8(void);
extern void isr_stub_9(void);
extern void isr_stub_10(void);
extern void isr_stub_11(void);
extern void isr_stub_12(void);
extern void isr_stub_13(void);
extern void isr_stub_14(void);
extern void isr_stub_15(void);
extern void isr_stub_16(void);
extern void isr_stub_17(void);
extern void isr_stub_18(void);
extern void isr_stub_19(void);
extern void isr_stub_20(void);
extern void isr_stub_21(void);
extern void isr_stub_22(void);
extern void isr_stub_23(void);
extern void isr_stub_24(void);
extern void isr_stub_25(void);
extern void isr_stub_26(void);
extern void isr_stub_27(void);
extern void isr_stub_28(void);
extern void isr_stub_29(void);
extern void isr_stub_30(void);
extern void isr_stub_31(void);
extern void isr_stub_32(void);
extern void isr_stub_33(void);
extern void isr_stub_34(void);
extern void isr_stub_35(void);
extern void isr_stub_36(void);
extern void isr_stub_37(void);
extern void isr_stub_38(void);
extern void isr_stub_39(void);
extern void isr_stub_40(void);
extern void isr_stub_41(void);
extern void isr_stub_42(void);
extern void isr_stub_43(void);
extern void isr_stub_44(void);
extern void isr_stub_45(void);
extern void isr_stub_46(void);
extern void isr_stub_47(void);

/* ── Stub address table (indexed by vector) ────────────────── */
static uint32_t g_stub_table[] = {
    (uint32_t)isr_stub_0,  (uint32_t)isr_stub_1,
    (uint32_t)isr_stub_2,  (uint32_t)isr_stub_3,
    (uint32_t)isr_stub_4,  (uint32_t)isr_stub_5,
    (uint32_t)isr_stub_6,  (uint32_t)isr_stub_7,
    (uint32_t)isr_stub_8,  (uint32_t)isr_stub_9,
    (uint32_t)isr_stub_10, (uint32_t)isr_stub_11,
    (uint32_t)isr_stub_12, (uint32_t)isr_stub_13,
    (uint32_t)isr_stub_14, (uint32_t)isr_stub_15,
    (uint32_t)isr_stub_16, (uint32_t)isr_stub_17,
    (uint32_t)isr_stub_18, (uint32_t)isr_stub_19,
    (uint32_t)isr_stub_20, (uint32_t)isr_stub_21,
    (uint32_t)isr_stub_22, (uint32_t)isr_stub_23,
    (uint32_t)isr_stub_24, (uint32_t)isr_stub_25,
    (uint32_t)isr_stub_26, (uint32_t)isr_stub_27,
    (uint32_t)isr_stub_28, (uint32_t)isr_stub_29,
    (uint32_t)isr_stub_30, (uint32_t)isr_stub_31,
    (uint32_t)isr_stub_32, (uint32_t)isr_stub_33,
    (uint32_t)isr_stub_34, (uint32_t)isr_stub_35,
    (uint32_t)isr_stub_36, (uint32_t)isr_stub_37,
    (uint32_t)isr_stub_38, (uint32_t)isr_stub_39,
    (uint32_t)isr_stub_40, (uint32_t)isr_stub_41,
    (uint32_t)isr_stub_42, (uint32_t)isr_stub_43,
    (uint32_t)isr_stub_44, (uint32_t)isr_stub_45,
    (uint32_t)isr_stub_46, (uint32_t)isr_stub_47,
};

/* ── public functions ──────────────────────────────────────── */

os_status_t idt_init(void) {
    uint8_t flags = IDT_PRESENT | IDT_DPL0 | IDT_GATE_INT32;

    /* Set up all gates */
    for (uint32_t i = 0; i < 48; i++) {
        idt_set_gate((uint8_t)i, g_stub_table[i], flags);
    }

    /* Load the IDT */
    g_idt_ptr.limit = (uint16_t)(sizeof(g_idt) - 1);
    g_idt_ptr.base  = (uint32_t)&g_idt;

    /* The idt_flush call must be done from a function that doesn't
       return via a normal ret — but idt_flush does lidt; ret which is fine. */
    idt_flush(&g_idt_ptr);

    return OS_OK;
}

os_status_t idt_set_gate(uint8_t vector, uint32_t handler, uint8_t flags) {
    g_idt[vector].offset_low  = (uint16_t)(handler & 0xFFFF);
    g_idt[vector].offset_high = (uint16_t)((handler >> 16) & 0xFFFF);
    g_idt[vector].selector    = 0x08;   /* kernel code segment */
    g_idt[vector].zero        = 0;
    g_idt[vector].flags       = flags;
    return OS_OK;
}
