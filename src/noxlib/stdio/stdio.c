/**
 * @file    noxlib/stdio/stdio.c
 * @brief   Formatted I/O — printf / snprintf family + char I/O
 *
 * Supported format specifiers:
 *   %c  %s  %d  %i  %u  %x  %X  %p  %%
 *   Width modifier:  %5d  %-5d  %05d
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdarg.h>

/* ── Character I/O ──────────────────────────────────────────────────── */

int putchar(int c)
{
    char ch = (char)c;
    return (write(STDOUT_FILENO, &ch, 1) == 1) ? (unsigned char)ch : -1;
}

int puts(const char *s)
{
    size_t len = strlen(s);
    if (write(STDOUT_FILENO, s, len)  != (ssize_t)len) return -1;
    if (write(STDOUT_FILENO, "\n", 1) != 1)            return -1;
    return 0;
}

int getchar(void)
{
    char c;
    return (read(STDIN_FILENO, &c, 1) == 1) ? (unsigned char)c : -1;
}

char *fgets(char *buf, int size, int fd)
{
    if (size <= 0) return NULL;
    int i = 0;
    while (i < size - 1) {
        char    c;
        ssize_t r = read(fd, &c, 1);
        if (r <= 0) break;
        buf[i++] = c;
        if (c == '\n') break;
    }
    buf[i] = '\0';
    return (i == 0) ? NULL : buf;
}

/* ── vsnprintf / vprintf core ───────────────────────────────────────── */

/*
 * Print context: either write to a sized buffer, or emit directly to
 * stdout one character at a time.  pos always counts total characters
 * that would have been written (like snprintf semantics).
 */
typedef struct {
    char  *buf;        /* destination buffer (NULL for stdout mode) */
    size_t cap;        /* buffer capacity including NUL             */
    size_t pos;        /* characters written so far                 */
    int    to_stdout;  /* 1 = write(1, ...) each char               */
} _ctx_t;

static void _emit(_ctx_t *ctx, char c)
{
    if (ctx->to_stdout) {
        /*
         * IMPORTANT: never pass &c directly to write().
         * With -O2 inlining, GCC may keep 'c' in a byte register (AL) and
         * &c could produce a stale or fictitious stack address.
         * Using a local array forces a proper stack spill with a valid address.
         */
        char buf[1];
        buf[0] = c;
        write(STDOUT_FILENO, buf, 1);
    } else {
        /* Write only if there is room for the char AND the trailing NUL. */
        if (ctx->cap > 1 && ctx->pos < ctx->cap - 1)
            ctx->buf[ctx->pos] = c;
    }
    ctx->pos++;
}

/* Emit a NUL-terminated string with optional width / alignment / pad. */
static void _emit_str(_ctx_t *ctx, const char *s,
                      int width, int left, int zero)
{
    int len = (int)strlen(s);
    int pad = width - len;

    if (!left && pad > 0) {
        char fill = zero ? '0' : ' ';
        for (int i = 0; i < pad; i++) _emit(ctx, fill);
    }
    while (*s) _emit(ctx, *s++);
    if (left && pad > 0)
        for (int i = 0; i < pad; i++) _emit(ctx, ' ');
}

/* Emit an unsigned integer in the given base. */
static void _emit_uint(_ctx_t *ctx, unsigned long val, int base, int upper,
                       int width, int left, int zero)
{
    static const char lc[] = "0123456789abcdef";
    static const char uc[] = "0123456789ABCDEF";
    const char *dig = upper ? uc : lc;

    char  tmp[24];
    int   len = 0;

    if (val == 0) {
        tmp[len++] = '0';
    } else {
        while (val) {
            tmp[len++] = dig[val % (unsigned)base];
            val /= (unsigned)base;
        }
    }
    /* Reverse into a local buffer. */
    char str[24];
    for (int i = 0; i < len; i++) str[i] = tmp[len - 1 - i];
    str[len] = '\0';

    _emit_str(ctx, str, width, left, zero);
}

static int _vprint(_ctx_t *ctx, const char *fmt, va_list ap)
{
    for (; *fmt; fmt++) {
        if (*fmt != '%') { _emit(ctx, *fmt); continue; }
        fmt++;                 /* skip '%' */

        /* ── Flags ───────────────────────────────────────── */
        int left = 0, zero = 0;
        while (*fmt == '-' || *fmt == '0') {
            if (*fmt == '-') left = 1;
            else             zero = 1;
            fmt++;
        }
        if (left) zero = 0;    /* '-' overrides '0' */

        /* ── Width ───────────────────────────────────────── */
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');

        /* ── Conversion ──────────────────────────────────── */
        switch (*fmt) {
        case 'c': {
            char c  = (char)va_arg(ap, int);
            char s[2] = { c, '\0' };
            _emit_str(ctx, s, width, left, 0);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            _emit_str(ctx, s ? s : "(null)", width, left, 0);
            break;
        }
        case 'd':
        case 'i': {
            long v = (long)va_arg(ap, int);
            if (v < 0) {
                _emit(ctx, '-');
                _emit_uint(ctx, (unsigned long)-v, 10, 0,
                           width ? width - 1 : 0, left, zero);
            } else {
                _emit_uint(ctx, (unsigned long)v, 10, 0, width, left, zero);
            }
            break;
        }
        case 'u': {
            unsigned long v = (unsigned long)va_arg(ap, unsigned int);
            _emit_uint(ctx, v, 10, 0, width, left, zero);
            break;
        }
        case 'x': {
            unsigned long v = (unsigned long)va_arg(ap, unsigned int);
            _emit_uint(ctx, v, 16, 0, width, left, zero);
            break;
        }
        case 'X': {
            unsigned long v = (unsigned long)va_arg(ap, unsigned int);
            _emit_uint(ctx, v, 16, 1, width, left, zero);
            break;
        }
        case 'p': {
            unsigned long v = (unsigned long)va_arg(ap, void *);
            _emit(ctx, '0'); _emit(ctx, 'x');
            _emit_uint(ctx, v, 16, 0, 8, 0, 1);
            break;
        }
        case '%':
            _emit(ctx, '%');
            break;
        default:
            _emit(ctx, '%');
            _emit(ctx, *fmt);
            break;
        }
    }

    /* NUL-terminate the buffer if writing to one. */
    if (!ctx->to_stdout && ctx->buf && ctx->cap > 0) {
        size_t nul = ctx->pos < ctx->cap ? ctx->pos : ctx->cap - 1;
        ctx->buf[nul] = '\0';
    }

    return (int)ctx->pos;
}

/* ── Public API ─────────────────────────────────────────────────────── */

int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap)
{
    _ctx_t ctx = { buf, n, 0, 0 };
    return _vprint(&ctx, fmt, ap);
}

int vprintf(const char *fmt, va_list ap)
{
    _ctx_t ctx = { NULL, 0, 0, 1 };
    return _vprint(&ctx, fmt, ap);
}

int snprintf(char *buf, size_t n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    /* Use a large cap — sprintf has no bounds, we just need the NUL written. */
    int r = vsnprintf(buf, (size_t)0x7fffffffu, fmt, ap);
    va_end(ap);
    return r;
}

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vprintf(fmt, ap);
    va_end(ap);
    return r;
}
