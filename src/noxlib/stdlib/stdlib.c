/**
 * @file    noxlib/stdlib/stdlib.c
 * @brief   exit, atoi/strtol, abs, errno
 */
#include <stdlib.h>
#include <stdint.h>

/* ── errno ─────────────────────────────────────────────────── */

int errno = 0;

/* ── exit ──────────────────────────────────────────────────── */

/* _exit is the raw syscall, defined in syscall.asm. */
extern void _exit(int status);

void exit(int status)
{
    /* TODO: walk atexit handlers, flush buffered stdio when added. */
    _exit(status);
}

/* ── Integer utilities ─────────────────────────────────────── */

int abs(int x)
{
    return x < 0 ? -x : x;
}

/* ── strtol ────────────────────────────────────────────────── */

long strtol(const char *s, char **endptr, int base)
{
    long result = 0;
    int  neg    = 0;

    /* Skip leading whitespace. */
    while (*s == ' ' || *s == '\t' || *s == '\n') s++;

    /* Optional sign. */
    if      (*s == '-') { neg = 1; s++; }
    else if (*s == '+') {           s++; }

    /* Auto-detect base. */
    if (base == 0) {
        if (*s == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (*s == '0')                              { base = 8;  s++;   }
        else                                             { base = 10;        }
    } else if (base == 16 && *s == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }

    const char *start = s;

    while (*s) {
        int digit;
        if      (*s >= '0' && *s <= '9') digit = *s - '0';
        else if (*s >= 'a' && *s <= 'z') digit = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') digit = *s - 'A' + 10;
        else break;
        if (digit >= base) break;
        result = result * base + digit;
        s++;
    }

    if (endptr) *endptr = (char *)(s == start ? start : s);
    return neg ? -result : result;
}

unsigned long strtoul(const char *s, char **endptr, int base)
{
    return (unsigned long)strtol(s, endptr, base);
}

int atoi(const char *s)
{
    return (int)strtol(s, NULL, 10);
}

long atol(const char *s)
{
    return strtol(s, NULL, 10);
}
