/**
 * @file    kernel/panic.c
 * @brief   Kernel panic — dumps state and halts
 * @author  Noxis Team
 * @date    2026-05-29
 */
#include <kernel/panic.h>
#include <common/types.h>

/* ── VGA constants ─────────────────────────────────────────── */
#define VGA_BUFFER   ((volatile uint16_t*)0xB8000)
#define VGA_WIDTH    80
#define VGA_RED      0x4
#define VGA_BG_BLUE  0x1

/* ── private functions ─────────────────────────────────────── */

static void _panic_putc(uint32_t row, uint32_t col, uint8_t c) {
    VGA_BUFFER[row * VGA_WIDTH + col] =
        (uint16_t)c | (uint16_t)((VGA_BG_BLUE << 4 | VGA_RED) << 8);
}

static void _panic_write(uint32_t* row, uint32_t* col, const uint8_t* str) {
    for (uint32_t i = 0; str[i] != '\0'; i++) {
        if (str[i] == '\n') {
            (*row)++;
            *col = 0;
        } else {
            _panic_putc(*row, *col, str[i]);
            (*col)++;
            if (*col >= VGA_WIDTH) {
                *col = 0;
                (*row)++;
            }
        }
        if (*row >= 25) *row = 0;
    }
}

static const uint8_t _hex_chars[] = "0123456789ABCDEF";

static void _panic_hex(uint32_t* row, uint32_t* col, uint32_t val) {
    for (int32_t i = 28; i >= 0; i -= 4) {
        _panic_putc(*row, *col, _hex_chars[(val >> i) & 0xF]);
        (*col)++;
    }
}

/* ── public functions ──────────────────────────────────────── */

void kernel_panic(const uint8_t* msg, isr_frame_t* frame) {
    uint32_t row = 0;
    uint32_t col = 0;

    /* Clear screen with red-on-blue */
    for (uint32_t i = 0; i < VGA_WIDTH * 25; i++) {
        VGA_BUFFER[i] = (uint16_t)' ' |
            (uint16_t)((VGA_BG_BLUE << 4 | VGA_RED) << 8);
    }

    _panic_write(&row, &col, (const uint8_t*)"KERNEL PANIC\n");
    _panic_write(&row, &col, msg);

    if (frame) {
        _panic_write(&row, &col, (const uint8_t*)"\n\n");
        _panic_write(&row, &col, (const uint8_t*)"EAX="); _panic_hex(&row, &col, frame->eax);
        _panic_write(&row, &col, (const uint8_t*)" ECX="); _panic_hex(&row, &col, frame->ecx);
        _panic_write(&row, &col, (const uint8_t*)" EDX="); _panic_hex(&row, &col, frame->edx);
        _panic_write(&row, &col, (const uint8_t*)"\n");
        _panic_write(&row, &col, (const uint8_t*)"EBX="); _panic_hex(&row, &col, frame->ebx);
        _panic_write(&row, &col, (const uint8_t*)" ESI="); _panic_hex(&row, &col, frame->esi);
        _panic_write(&row, &col, (const uint8_t*)" EDI="); _panic_hex(&row, &col, frame->edi);
        _panic_write(&row, &col, (const uint8_t*)"\n");
        _panic_write(&row, &col, (const uint8_t*)"EBP="); _panic_hex(&row, &col, frame->ebp);
        _panic_write(&row, &col, (const uint8_t*)" ESP="); _panic_hex(&row, &col, frame->user_esp);
        _panic_write(&row, &col, (const uint8_t*)"\n");
        _panic_write(&row, &col, (const uint8_t*)"EIP="); _panic_hex(&row, &col, frame->eip);
        _panic_write(&row, &col, (const uint8_t*)"\n");
    }

    /* Halt forever */
    for (;;);
}
