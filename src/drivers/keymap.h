/**
 * @file    drivers/keymap.h
 * @brief   Switchable keyboard layouts.
 *
 * A layout maps scancode-set-1 codes (0..127) to characters, with a
 * separate table for the shifted state.  Layouts are registered at boot;
 * the active one is consulted by the keyboard ISR.  Switching is exposed
 * to userland through the synthetic files /proc/keymap (read) and
 * /dev/keymap (write a layout name to switch).
 */
#ifndef DRIVERS_KEYMAP_H
#define DRIVERS_KEYMAP_H

#include <common/types.h>

#define KEYMAP_NAME_MAX  16

typedef struct {
    char    name[KEYMAP_NAME_MAX];   /* e.g. "us", "fr" */
    uint8_t low [128];               /* unshifted scancode → char */
    uint8_t high[128];               /* shifted   scancode → char */
} keymap_t;

/* Build the built-in layouts and select the default ("us"). */
void            keymap_init(void);

/* Active layout used by the keyboard ISR. */
const keymap_t *keymap_active(void);

/* Switch the active layout by name. Returns 1 on success, 0 if unknown. */
int             keymap_set(const char *name);

/* Introspection (for /proc/keymap). */
uint32_t        keymap_count(void);
const keymap_t *keymap_at(uint32_t i);
const char     *keymap_active_name(void);

#endif /* DRIVERS_KEYMAP_H */
