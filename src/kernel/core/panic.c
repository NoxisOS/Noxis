/**
 * @file    kernel/panic.c
 * @brief   Kernel panic — dumps state, stack trace, and halts
 * @author  Noxis Team
 */
#include <kernel/core/panic.h>
#include <common/types.h>

/* ── VGA constants ─────────────────────────────────────────── */
#define VGA_BUFFER   ((volatile uint16_t*)0xB8000)
#define VGA_WIDTH    80
#define VGA_HEIGHT   25
#define VGA_WHITE    0xF
#define VGA_RED      0x4
#define VGA_YELLOW   0xE
#define VGA_BG_RED   0x4

/* colour: attr = (bg << 4) | fg */
#define VGA_BLINK    0x8   /* bit 3 of fg = blink/bright-bg in text mode */
#define ATTR_TITLE   ((uint8_t)((VGA_BG_RED << 4) | VGA_WHITE | VGA_BLINK)) /* bright white on red — stands out */
#define ATTR_REGS    ((uint8_t)((VGA_BG_RED << 4) | VGA_YELLOW))            /* yellow on red */
#define ATTR_NORMAL  ((uint8_t)((VGA_BG_RED << 4) | VGA_WHITE))             /* white on red */

/* ── private state ─────────────────────────────────────────── */
static uint8_t _cur_attr = ATTR_NORMAL;

/* ── private functions ─────────────────────────────────────── */

static void _panic_putc(uint32_t row, uint32_t col, uint8_t c) {
    if (row >= VGA_HEIGHT || col >= VGA_WIDTH) return;
    VGA_BUFFER[row * VGA_WIDTH + col] =
        (uint16_t)c | ((uint16_t)_cur_attr << 8);
}

static void _panic_write(uint32_t* row, uint32_t* col, const uint8_t* str) {
    for (uint32_t i = 0; str[i] != '\0'; i++) {
        if (str[i] == '\n') {
            (*row)++;
            *col = 0;
        } else {
            _panic_putc(*row, *col, str[i]);
            (*col)++;
            if (*col >= VGA_WIDTH) { *col = 0; (*row)++; }
        }
        if (*row >= VGA_HEIGHT) *row = VGA_HEIGHT - 1;
    }
}

static const uint8_t _hex_chars[] = "0123456789ABCDEF";

static void _panic_hex(uint32_t* row, uint32_t* col, uint32_t val) {
    _panic_write(row, col, (const uint8_t*)"0x");
    for (int32_t i = 28; i >= 0; i -= 4) {
        _panic_putc(*row, *col, _hex_chars[(val >> i) & 0xF]);
        (*col)++;
    }
}

static void _panic_dec(uint32_t* row, uint32_t* col, uint32_t val) {
    char tmp[12]; int i = 0;
    if (val == 0) { _panic_write(row, col, (const uint8_t*)"0"); return; }
    while (val) { tmp[i++] = '0' + (val % 10); val /= 10; }
    while (i--) { _panic_putc(*row, *col, (uint8_t)tmp[i]); (*col)++; }
}

/* ── stack trace ───────────────────────────────────────────── */

static void _panic_stacktrace(uint32_t* row, uint32_t* col, uint32_t ebp) {
    /* Kernel stack lives between 1 MB and the higher-half boundary.
       Only dereference EBP if both the frame pointer AND the return
       address slot (ebp+4) are within that safe window — anything else
       risks a double-fault inside the panic handler itself. */
#define KSTACK_LO  0x00100000u   /* 1 MB — below this is firmware/BIOS */
#define KSTACK_HI  0xC0000000u   /* higher-half kernel base */

    _panic_write(row, col, (const uint8_t*)"\nStack trace:\n");
    _cur_attr = ATTR_REGS;
    for (int i = 0; i < 8; i++) {
        /* Validate the frame pointer itself, and that ebp+4 is readable. */
        if (ebp < KSTACK_LO || ebp + 4 >= KSTACK_HI) break;
        uint32_t* frame_ptr = (uint32_t*)ebp;
        uint32_t  next_ebp  = frame_ptr[0];
        uint32_t  ret_addr  = frame_ptr[1];
        /* A valid return address must also be in kernel space. */
        if (ret_addr < KSTACK_LO || ret_addr >= KSTACK_HI) break;
        _panic_write(row, col, (const uint8_t*)"  #");
        _panic_dec(row, col, (uint32_t)i);
        _panic_write(row, col, (const uint8_t*)"  ");
        _panic_hex(row, col, ret_addr);
        _panic_write(row, col, (const uint8_t*)"\n");
        /* Guard against non-advancing EBP chains (corrupt stack). */
        if (next_ebp <= ebp) break;
        ebp = next_ebp;
    }
    _cur_attr = ATTR_NORMAL;  /* always restored, even on early break */
}

/* ── public functions ──────────────────────────────────────── */

void kernel_panic(const uint8_t* msg, isr_frame_t* frame) {
    /* Disable interrupts immediately */
    __asm__ __volatile__("cli");

    /* Fill screen: white on red */
    _cur_attr = ATTR_NORMAL;
    for (uint32_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        VGA_BUFFER[i] = (uint16_t)' ' | ((uint16_t)ATTR_NORMAL << 8);

    /* Title bar: white on red, bold-looking with spaces */
    uint32_t row = 0, col = 0;
    _cur_attr = ATTR_TITLE;
    for (uint32_t c = 0; c < VGA_WIDTH; c++)
        _panic_putc(0, c, ' ');
    col = 2;
    _panic_write(&row, &col, (const uint8_t*)"*** KERNEL PANIC ***");
    row = 1; col = 0;
    _cur_attr = ATTR_NORMAL;

    /* Message */
    _panic_write(&row, &col, (const uint8_t*)"\n  ");
    _panic_write(&row, &col, msg);
    _panic_write(&row, &col, (const uint8_t*)"\n");

    if (frame) {
        /* CR2 — faulting address (always read, harmless if not a PF) */
        uint32_t cr2 = 0;
        __asm__ __volatile__("mov %%cr2, %0" : "=r"(cr2));

        _panic_write(&row, &col, (const uint8_t*)"\n");
        _cur_attr = ATTR_REGS;

        _panic_write(&row, &col, (const uint8_t*)"  EAX="); _panic_hex(&row, &col, frame->eax);
        _panic_write(&row, &col, (const uint8_t*)"  ECX="); _panic_hex(&row, &col, frame->ecx);
        _panic_write(&row, &col, (const uint8_t*)"  EDX="); _panic_hex(&row, &col, frame->edx);
        _panic_write(&row, &col, (const uint8_t*)"\n");
        _panic_write(&row, &col, (const uint8_t*)"  EBX="); _panic_hex(&row, &col, frame->ebx);
        _panic_write(&row, &col, (const uint8_t*)"  ESI="); _panic_hex(&row, &col, frame->esi);
        _panic_write(&row, &col, (const uint8_t*)"  EDI="); _panic_hex(&row, &col, frame->edi);
        _panic_write(&row, &col, (const uint8_t*)"\n");
        _panic_write(&row, &col, (const uint8_t*)"  EBP="); _panic_hex(&row, &col, frame->ebp);
        _panic_write(&row, &col, (const uint8_t*)"  ESP="); _panic_hex(&row, &col, frame->user_esp);
        _panic_write(&row, &col, (const uint8_t*)"\n");
        _panic_write(&row, &col, (const uint8_t*)"  EIP="); _panic_hex(&row, &col, frame->eip);
        _panic_write(&row, &col, (const uint8_t*)"  ERR="); _panic_hex(&row, &col, frame->error_code);
        _panic_write(&row, &col, (const uint8_t*)"  CR2="); _panic_hex(&row, &col, cr2);
        _panic_write(&row, &col, (const uint8_t*)"\n");
        _panic_write(&row, &col, (const uint8_t*)"  VEC="); _panic_dec(&row, &col, frame->vector);

        _cur_attr = ATTR_NORMAL;

        /* Stack trace from EBP */
        _panic_stacktrace(&row, &col, frame->ebp);
    }

    /* Bottom bar */
    if (row < VGA_HEIGHT - 1) row = VGA_HEIGHT - 1;
    col = 0;
    _cur_attr = ATTR_TITLE;
    for (uint32_t c = 0; c < VGA_WIDTH; c++) _panic_putc(row, c, ' ');
    col = 2;
    _panic_write(&row, &col, (const uint8_t*)"System halted. Please reboot.");

    /* Hard halt */
    __asm__ __volatile__("cli; hlt");
    for (;;) __asm__ __volatile__("hlt");
}
