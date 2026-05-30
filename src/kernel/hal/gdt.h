/**
 * @file    hal/gdt.h
 * @brief   Global Descriptor Table definitions
 * @author  Noxis Team
 * @date    2026-05-29
 */
#ifndef HAL_GDT_H
#define HAL_GDT_H

#include <common/types.h>
#include <common/status.h>

/* ── constants ─────────────────────────────────────────────── */
#define GDT_NULL       0
#define GDT_KERNEL_CS  1
#define GDT_KERNEL_DS  2
#define GDT_USER_CS    3
#define GDT_USER_DS    4
#define GDT_TSS        5
#define GDT_ENTRIES    6

/* ── TSS descriptor access byte ────────────────────────────── */
#define GDT_TSS_AVAIL   0x9   /* 32-bit TSS available */

/* ── segment selector macros ───────────────────────────────── */
#define SELECTOR(idx, rpl)  ((uint16_t)(((idx) << 3) | ((rpl) & 3)))

/* ── access byte flags ─────────────────────────────────────── */
#define GDT_PRESENT    0x80
#define GDT_DPL0       0x00
#define GDT_DPL3       0x60
#define GDT_SYSTEM     0x00
#define GDT_CODE_DATA  0x10
#define GDT_EXEC       0x08
#define GDT_DC         0x04
#define GDT_RW         0x02
#define GDT_ACCESSED   0x01

/* ── granularity flags (nibble values for the flags field) ── */
#define GDT_GRAN_1B    0x0   /* byte granularity */
#define GDT_GRAN_4K    0x8   /* 4 KB granularity */
#define GDT_SIZE_16    0x0   /* 16-bit protected mode */
#define GDT_SIZE_32    0x4   /* 32-bit protected mode */

/* ── types ─────────────────────────────────────────────────── */

/**
 * @brief A single GDT entry (8 bytes, hardware format)
 */
typedef struct __attribute__((packed)) {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} gdt_entry_t;

/**
 * @brief TSS entry spans 2 GDT slots (first is the actual descriptor)
 */
typedef struct __attribute__((packed)) {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
    uint32_t base_upper;  /* high 32 bits of base (unused in 32-bit) */
    uint32_t reserved;
} tss_descriptor_t;

/**
 * @brief 32-bit Task State Segment (minimum for ring switching)
 */
typedef struct __attribute__((packed)) {
    uint16_t prev_link; uint16_t _pad0;
    uint32_t esp0;       /* ring 0 stack */
    uint16_t ss0;  uint16_t _pad1;
    uint32_t esp1; uint16_t ss1; uint16_t _pad2;
    uint32_t esp2; uint16_t ss2; uint16_t _pad3;
    uint32_t cr3;
    uint32_t eip, eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint16_t es;  uint16_t _pad4;
    uint16_t cs;  uint16_t _pad5;
    uint16_t ss;  uint16_t _pad6;
    uint16_t ds;  uint16_t _pad7;
    uint16_t fs;  uint16_t _pad8;
    uint16_t gs;  uint16_t _pad9;
    uint16_t ldt; uint16_t _pad10;
    uint16_t trap;
    uint16_t iomap_base;
} tss_t;

/**
 * @brief GDTR structure passed to lgdt
 */
typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint32_t base;
} gdt_ptr_t;

/* ── public functions ──────────────────────────────────────── */

/**
 * @brief Sets the ring 0 stack in the TSS (for user→kernel transitions)
 * @param esp  Ring 0 stack pointer
 */
void gdt_set_kernel_stack(uint32_t esp);

/**
 * @brief Initializes the GDT with kernel/user segments and TSS
 * @return OS_OK
 */
os_status_t gdt_init(void);

#endif /* HAL_GDT_H */
