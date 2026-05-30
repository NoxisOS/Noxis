/**
 * @file    drivers/tty/tty.h
 * @brief   TTY subsystem — line discipline, canonical/raw mode, signals
 * @author  Noxis Team
 * @date    2026-05-30
 */
#ifndef DRIVERS_TTY_TTY_H
#define DRIVERS_TTY_TTY_H

#include <common/types.h>
#include <common/status.h>

#define TTY_BUF_SIZE  256

/* ── ioctl requests ─────────────────────────────────────────── */
#define TCGETS  1
#define TCSETS  2

/* ── lflag bits ─────────────────────────────────────────────── */
#define ISIG    0x01
#define ICANON  0x02
#define ECHO    0x04

/* ── termios (simplified) ───────────────────────────────────── */
typedef struct {
    uint32_t lflag;
} termios_t;

/**
 * @brief Feed a raw character from the keyboard ISR into the TTY.
 *        Handles echo, line editing, signal generation.
 *        Safe to call from interrupt context.
 */
void tty_input(uint8_t c);

/**
 * @brief Blocking read from the TTY.
 *        In canonical mode: returns one line (including '\n').
 *        In raw mode: returns available chars immediately.
 *        Returns 0 on EOF (Ctrl+D at line start in canonical mode).
 */
int32_t tty_read(uint8_t* buf, uint32_t len);

/**
 * @brief ioctl for the TTY. TCGETS/TCSETS for terminal mode.
 */
int32_t tty_ioctl(uint32_t req, void* arg);

/**
 * @brief Initialize the TTY subsystem.
 */
os_status_t tty_init(void);

#endif /* DRIVERS_TTY_TTY_H */
