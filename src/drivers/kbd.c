/**
 * @file    drivers/kbd.c
 * @brief   PS/2 keyboard — IRQ1 handler, scancode→ASCII
 * @author  Noxis Team
 * @date    2026-05-30
 */
#include <drivers/kbd.h>
#include <drivers/tty/tty.h>
#include <kernel/isr/isr.h>
#include <kernel/hal/ports.h>
#include <kernel/hal/pic.h>
#include <common/types.h>

/* ── PS/2 port constants ────────────────────────────────────── */
#define KBD_DATA        0x60
#define KBD_STATUS      0x64
#define KBD_OUTPUT_FULL 0x01

/* ── scancodes for shift / caps lock / ctrl ─────────────────── */
#define SC_LSHIFT 0x2A
#define SC_RSHIFT 0x36
#define SC_CAPS   0x3A
#define SC_LCTRL  0x1D
#define SC_RELEASE 0x80

/* ── file-scope state ──────────────────────────────────────── */
static volatile uint8_t  g_shift;
static volatile uint8_t  g_caps;
static volatile uint8_t  g_ctrl;

/* ── scancode set 1 (XT) translation tables ─────────────────── */

static const uint8_t _sc_low[128] = {
    0,    27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,   'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'','`',  0,   '\\','z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/',  0,  '*',  0,  ' ',  0,    0,   0,   0,   0,   0,
};

static const uint8_t _sc_high[128] = {
    0,    27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,   'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',  0,   '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?',  0,  '*',  0,  ' ',  0,    0,   0,   0,   0,   0,
};

/* ── ISR ───────────────────────────────────────────────────── */

static void _kbd_isr(isr_frame_t* frame) {
    (void)frame;
    uint8_t sc = port_byte_in(KBD_DATA);

    uint8_t release = sc & SC_RELEASE;
    uint8_t code    = sc & 0x7F;

    if (code == SC_LSHIFT || code == SC_RSHIFT) {
        if (release) { if (g_shift) g_shift--; }
        else         { g_shift++; }
        return;
    }
    if (code == SC_LCTRL) {
        if (release) { if (g_ctrl) g_ctrl--; }
        else         { g_ctrl++; }
        return;
    }
    if (code == SC_CAPS) {
        if (!release) g_caps = !g_caps;
        return;
    }

    if (release) return;

    uint8_t c = g_shift ? _sc_high[code] : _sc_low[code];
    if (!c) return;

    if (g_caps) {
        if      (c >= 'a' && c <= 'z') c = (uint8_t)(c - 32);
        else if (c >= 'A' && c <= 'Z') c = (uint8_t)(c + 32);
    }

    if (g_ctrl) {
        if (c >= 'a' && c <= 'z') c = (uint8_t)(c & 0x1F);
        else if (c >= 'A' && c <= 'Z') c = (uint8_t)(c & 0x1F);
    }

    tty_input(c);
}

/* ── public functions ──────────────────────────────────────── */

os_status_t kbd_init(void) {
    g_shift = g_caps = g_ctrl = 0;

    while (port_byte_in(KBD_STATUS) & KBD_OUTPUT_FULL) {
        (void)port_byte_in(KBD_DATA);
    }

    os_status_t s = isr_register_handler(0x21, _kbd_isr);
    if (s != OS_OK) return s;

    pic_unmask(1);
    return OS_OK;
}

int32_t kbd_poll(void) { return -1; }
uint8_t kbd_getchar(void) { return 0; }
