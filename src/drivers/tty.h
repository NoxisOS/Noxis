/**
 * @file    drivers/tty.h
 * @brief   Terminal (TTY) state — termios subset for the system console.
 *
 * Flags subset (c_lflag):
 *   ISIG   — generate signals on Ctrl-C (SIGINT)
 *   ICANON — canonical (line-buffered) mode; clear for raw mode
 *   ECHO   — echo typed characters
 *   ECHOE  — echo erase (backspace) as BS-SP-BS
 *
 * Control chars (c_cc):
 *   VMIN   — minimum chars to satisfy a raw-mode read (default 1)
 *   VTIME  — timeout in deciseconds (0 = no timeout; not yet used)
 */
#ifndef DRIVERS_TTY_H
#define DRIVERS_TTY_H

#include <common/types.h>

/* c_lflag bits */
#define TTY_ISIG    0x0001u
#define TTY_ICANON  0x0002u
#define TTY_ECHO    0x0008u
#define TTY_ECHOE   0x0010u

/* c_cc indices */
#define TTY_VMIN   0
#define TTY_VTIME  1
#define TTY_NCC    8

typedef struct {
    uint32_t c_lflag;
    uint8_t  c_cc[TTY_NCC];
} ktermios_t;

/* The single global console termios (default: canonical + echo + signals). */
extern ktermios_t g_termios;

/* Initialise with sensible defaults. */
void tty_init(void);

/* Quick accessors used by other subsystems. */
static inline int tty_isig   (void) { return (g_termios.c_lflag & TTY_ISIG)   != 0; }
static inline int tty_canonical(void){ return (g_termios.c_lflag & TTY_ICANON) != 0; }
static inline int tty_echo   (void) { return (g_termios.c_lflag & TTY_ECHO)   != 0; }

#endif /* DRIVERS_TTY_H */
