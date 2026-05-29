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

/* ── granularity flags ─────────────────────────────────────── */
#define GDT_GRAN_1B    0x00
#define GDT_GRAN_4K    0x80
#define GDT_SIZE_16    0x00
#define GDT_SIZE_32    0x40

/* ── types ─────────────────────────────────────────────────── */

/**
 * @brief A single GDT entry (8 bytes, hardware format)
 */
typedef struct __attribute__((packed)) {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  limit_high : 4;
    uint8_t  flags      : 4;
    uint8_t  base_high;
} gdt_entry_t;

/**
 * @brief GDTR structure passed to lgdt
 */
typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint32_t base;
} gdt_ptr_t;

/* ── public functions ──────────────────────────────────────── */

/**
 * @brief Initializes the GDT with kernel/user segments and TSS.
 *        Replaces the temporary bootloader GDT.
 * @return OS_OK
 */
os_status_t gdt_init(void);

#endif /* HAL_GDT_H */
