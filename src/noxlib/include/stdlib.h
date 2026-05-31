/**
 * @file    noxlib/include/stdlib.h
 * @brief   Memory allocation, process control, numeric conversions
 */
#ifndef _NOXLIB_STDLIB_H
#define _NOXLIB_STDLIB_H

#include <stddef.h>

#define EXIT_SUCCESS  0
#define EXIT_FAILURE  1

/* ── Memory allocation ─────────────────────────────────── */
void *malloc (size_t size);
void *calloc (size_t nmemb, size_t size);
void  free   (void *ptr);

/* ── Process ───────────────────────────────────────────── */
void  exit  (int status);  /* flush + _exit   */
void _exit  (int status);  /* raw syscall     */

/* ── Numeric conversions ───────────────────────────────── */
int          atoi  (const char *s);
long         atol  (const char *s);
long         strtol (const char *s, char **endptr, int base);
unsigned long strtoul(const char *s, char **endptr, int base);

/* ── Misc ──────────────────────────────────────────────── */
int  abs(int x);

#endif /* _NOXLIB_STDLIB_H */
