/**
 * @file    drivers/tty.c
 * @brief   Global terminal state.
 */
#include <drivers/tty.h>

ktermios_t g_termios;

void tty_init(void) {
    g_termios.c_lflag = TTY_ISIG | TTY_ICANON | TTY_ECHO | TTY_ECHOE;
    for (int i = 0; i < TTY_NCC; i++) g_termios.c_cc[i] = 0;
    g_termios.c_cc[TTY_VMIN]  = 1;   /* raw-mode: return ≥ 1 char */
    g_termios.c_cc[TTY_VTIME] = 0;   /* no timeout */
}
