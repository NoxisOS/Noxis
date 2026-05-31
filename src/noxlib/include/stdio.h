/**
 * @file    noxlib/include/stdio.h
 * @brief   Formatted I/O
 */
#ifndef _NOXLIB_STDIO_H
#define _NOXLIB_STDIO_H

#include <stddef.h>
#include <stdarg.h>

/* ── Character I/O ──────────────────────────────────────── */
int   putchar(int c);
int   puts   (const char *s);
int   getchar(void);

/* fgets: reads from fd (use STDIN_FILENO=0 for stdin).
   Returns buf, or NULL on EOF / error. */
char *fgets(char *buf, int size, int fd);

/* ── Formatted output ───────────────────────────────────── */
int printf  (const char *fmt, ...);
int sprintf (char *buf, const char *fmt, ...);
int snprintf(char *buf, size_t n, const char *fmt, ...);
int vprintf (const char *fmt, va_list ap);
int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);

/*
 * Supported format specifiers:
 *   %c  %s  %d  %i  %u  %x  %X  %p  %%
 *   Width:      %5d   %-5d   %05d
 */

#endif /* _NOXLIB_STDIO_H */
