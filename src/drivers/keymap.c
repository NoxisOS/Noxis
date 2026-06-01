/**
 * @file    drivers/keymap.c
 * @brief   Built-in keyboard layouts + active-layout selection.
 *
 * Layouts are derived from the US QWERTY base at init: alternative
 * layouts copy the base and apply a small set of per-scancode overrides,
 * which keeps each layout definition compact and easy to add.
 */
#include <drivers/keymap.h>
#include <common/types.h>

/* ── US QWERTY base tables (scancode set 1) ──────────────────── */

static const uint8_t _us_low[128] = {
    0,    27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,   'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'','`',  0,   '\\','z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/',  0,  '*',  0,  ' ',  0,    0,   0,   0,   0,   0,
};
static const uint8_t _us_high[128] = {
    0,    27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,   'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',  0,   '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?',  0,  '*',  0,  ' ',  0,    0,   0,   0,   0,   0,
};

/* ── Per-scancode override entries for alternative layouts ────── */
typedef struct { uint8_t sc; uint8_t low; uint8_t high; } kov_t;

/* Simplified French AZERTY: the classic a/q, z/w swaps and m relocation.
   (Not the full accented top row — enough to be a usable, distinct layout
   and to demonstrate live switching.) */
static const kov_t _fr_over[] = {
    { 0x10, 'a', 'A' },   /* QWERTY q → a */
    { 0x1E, 'q', 'Q' },   /* QWERTY a → q */
    { 0x11, 'z', 'Z' },   /* QWERTY w → z */
    { 0x2C, 'w', 'W' },   /* QWERTY z → w */
    { 0x27, 'm', 'M' },   /* QWERTY ; → m */
    { 0x32, ',', '?' },   /* QWERTY m → , */
    { 0x33, ';', '.' },   /* , → ; */
};
#define FR_OVER_N (sizeof(_fr_over) / sizeof(_fr_over[0]))

/* ── Registry ────────────────────────────────────────────────── */

#define MAX_LAYOUTS 8
static keymap_t       g_layouts[MAX_LAYOUTS];
static uint32_t       g_n_layouts;
static const keymap_t *g_active;

static void _copy_name(char *dst, const char *src) {
    uint32_t i = 0;
    for (; src[i] && i < KEYMAP_NAME_MAX - 1; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static keymap_t *_add(const char *name) {
    if (g_n_layouts >= MAX_LAYOUTS) return (keymap_t*)0;
    keymap_t *m = &g_layouts[g_n_layouts++];
    _copy_name(m->name, name);
    for (uint32_t i = 0; i < 128; i++) { m->low[i] = _us_low[i]; m->high[i] = _us_high[i]; }
    return m;
}

void keymap_init(void) {
    g_n_layouts = 0;

    /* us — straight QWERTY base. */
    _add("us");

    /* fr — base + AZERTY overrides. */
    keymap_t *fr = _add("fr");
    if (fr) {
        for (uint32_t i = 0; i < FR_OVER_N; i++) {
            fr->low [_fr_over[i].sc] = _fr_over[i].low;
            fr->high[_fr_over[i].sc] = _fr_over[i].high;
        }
    }

    g_active = &g_layouts[0];
}

const keymap_t *keymap_active(void)      { return g_active; }
uint32_t        keymap_count(void)       { return g_n_layouts; }
const keymap_t *keymap_at(uint32_t i)    { return i < g_n_layouts ? &g_layouts[i] : (keymap_t*)0; }
const char     *keymap_active_name(void) { return g_active ? g_active->name : "?"; }

static int _name_eq(const char *a, const char *b) {
    uint32_t i = 0;
    while (a[i] && a[i] == b[i]) i++;
    return a[i] == '\0' && b[i] == '\0';
}

int keymap_set(const char *name) {
    for (uint32_t i = 0; i < g_n_layouts; i++) {
        if (_name_eq(g_layouts[i].name, name)) {
            g_active = &g_layouts[i];
            return 1;
        }
    }
    return 0;
}
