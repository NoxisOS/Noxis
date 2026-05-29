/**
 * @file    drivers/keyboard.c
 * @brief   PS/2 keyboard driver — IRQ1 ISR, scancode→ASCII, circular buffer
 * @author  Noxis Team
 * @date    2026-05-29
 */
#include <drivers/keyboard.h>
#include <kernel/isr.h>
#include <hal/ports.h>
#include <hal/pic.h>
#include <common/types.h>

/* ── keyboard port constants ───────────────────────────────── */
#define KBD_DATA_PORT    0x60
#define KBD_STATUS_PORT  0x64
#define KBD_IRQ          1
#define KBD_VECTOR       0x21       /* PIC master base + IRQ1 */

/* ── buffer constants ──────────────────────────────────────── */
#define KBD_BUFFER_SIZE  256

/* ── scancode flags ────────────────────────────────────────── */
#define SCANCODE_RELEASE 0x80

/* ── file-scope state ──────────────────────────────────────── */
static uint8_t  g_buffer[KBD_BUFFER_SIZE];
static uint32_t g_head;                 /* write index */
static uint32_t g_tail;                 /* read index */
static uint32_t g_count;                /* items in buffer */
static bool_t   g_shift;                /* left shift held */
static bool_t   g_capslock;             /* caps lock toggle */

/* ── US QWERTY scancode set 1 → ASCII (no shift) ───────────── */
static const uint8_t _scancode_ascii_noshift[128] = {
    0,    0,    '1', '2', '3', '4', '5', '6',    /* 0x00-0x07 */
    '7', '8', '9', '0', '-', '=', 0,   0,        /* 0x08-0x0F */
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',       /* 0x10-0x17 */
    'o', 'p', '[', ']', '\n', 0, 'a',  's',       /* 0x18-0x1F */
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',       /* 0x20-0x27 */
    '\'', '`', 0,  '\\', 'z', 'x', 'c', 'v',      /* 0x28-0x2F */
    'b', 'n', 'm', ',', '.', '/', 0,   '*',        /* 0x30-0x37 */
    0,    ' ', 0,   0,   0,   0,   0,   0,         /* 0x38-0x3F */
    0,   0,   0,   0,   0,   0,   0,   0,          /* 0x40-0x47 */
    0,   0,   0,   0,   0,   0,   0,   0,          /* 0x48-0x4F */
    0,   0,   0,   0,   0,   0,   0,   0,          /* 0x50-0x57 */
    0,   0,   0,   0,   0,   0,   0,   0,          /* 0x58-0x5F */
    0,   0,   0,   0,   0,   0,   0,   0,          /* 0x60-0x67 */
    0,   0,   0,   0,   0,   0,   0,   0,          /* 0x68-0x6F */
    0,   0,   0,   0,   0,   0,   0,   0,          /* 0x70-0x77 */
    0,   0,   0,   0,   0,   0,   0,   0,          /* 0x78-0x7F */
};

/* ── US QWERTY scancode set 1 → ASCII (with shift) ─────────── */
static const uint8_t _scancode_ascii_shift[128] = {
    0,    0,    '!', '@', '#', '$', '%', '^',       /* 0x00-0x07 */
    '&', '*', '(', ')', '_', '+', 0,   0,           /* 0x08-0x0F */
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',          /* 0x10-0x17 */
    'O', 'P', '{', '}', '\n', 0, 'A',  'S',          /* 0x18-0x1F */
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',          /* 0x20-0x27 */
    '"',  '~', 0,  '|',  'Z', 'X', 'C', 'V',         /* 0x28-0x2F */
    'B', 'N', 'M', '<', '>', '?', 0,   '*',          /* 0x30-0x37 */
    0,    ' ', 0,   0,   0,   0,   0,   0,           /* 0x38-0x3F */
    0,   0,   0,   0,   0,   0,   0,   0,            /* 0x40-0x4F */
    0,   0,   0,   0,   0,   0,   0,   0,            /* 0x48-0x4F */
    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,
};

/* ── private functions ─────────────────────────────────────── */

/**
 * @brief Pushes a character into the circular buffer (ISR context)
 * @return FALSE if buffer is full
 */
static bool_t _buffer_push(uint8_t c) {
    if (g_count >= KBD_BUFFER_SIZE) return FALSE;
    g_buffer[g_head] = c;
    g_head = (g_head + 1) % KBD_BUFFER_SIZE;
    g_count++;
    return TRUE;
}

/**
 * @brief Pops a character from the circular buffer (blocking)
 * @param out  Output: character read
 * @return TRUE if a character was available
 */
static bool_t _buffer_pop(uint8_t* out) {
    if (g_count == 0) return FALSE;
    *out = g_buffer[g_tail];
    g_tail = (g_tail + 1) % KBD_BUFFER_SIZE;
    g_count--;
    return TRUE;
}

/**
 * @brief Handles a key press scancode
 */
static void _handle_scancode(uint8_t scancode) {
    uint8_t ascii;

    /* Ignore extended key prefix (0xE0) — handle in ISR */
    if (scancode == 0xE0) return;

    /* Check for release (bit 7 set) */
    if (scancode & SCANCODE_RELEASE) {
        scancode &= ~SCANCODE_RELEASE;
        /* Left shift or right shift released */
        if (scancode == 0x2A || scancode == 0x36) {
            g_shift = FALSE;
        }
        return;
    }

    /* Modifier keys (pressed) */
    if (scancode == 0x2A || scancode == 0x36) {
        /* Left shift or right shift */
        g_shift = TRUE;
        return;
    }
    if (scancode == 0x3A) {
        /* Caps lock toggle */
        g_capslock = !g_capslock;
        return;
    }

    /* Backspace */
    if (scancode == 0x0E) {
        _buffer_push('\b');
        return;
    }

    /* Tab */
    if (scancode == 0x0F) {
        _buffer_push('\t');
        return;
    }

    /* Look up ASCII value */
    if (g_shift) {
        ascii = _scancode_ascii_shift[scancode];
    } else {
        ascii = _scancode_ascii_noshift[scancode];
    }

    /* Apply caps lock (only for letters) */
    if (!g_shift && g_capslock) {
        if (ascii >= 'a' && ascii <= 'z') {
            ascii = (uint8_t)(ascii - ('a' - 'A'));
        }
    }

    if (ascii != 0) {
        _buffer_push(ascii);
    }
}

/**
 * @brief Keyboard ISR — called from ISR dispatcher on IRQ1
 */
static void _kbd_isr(isr_frame_t* frame) {
    (void)frame; /* unused */

    /* Read scancode from port 0x60 */
    uint8_t scancode = port_byte_in(KBD_DATA_PORT);
    _handle_scancode(scancode);
}

/* ── public functions ──────────────────────────────────────── */

os_status_t kbd_init(void) {
    g_head = 0;
    g_tail = 0;
    g_count = 0;
    g_shift = FALSE;
    g_capslock = FALSE;

    os_status_t status = isr_register_handler(KBD_VECTOR, _kbd_isr);
    if (status != OS_OK) return status;

    /* Unmask IRQ1 in the PIC */
    pic_unmask(KBD_IRQ);

    return OS_OK;
}

os_status_t kbd_read(uint8_t* out) {
    if (!out) return OS_ERR_NULL;

    /* Block until a character is available */
    while (!_buffer_pop(out)) {
        /* Spin-wait — will be replaced with scheduler yield later */
    }

    return OS_OK;
}

bool_t kbd_has_data(void) {
    return (g_count > 0) ? TRUE : FALSE;
}
