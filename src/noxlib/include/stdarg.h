/**
 * @file    noxlib/include/stdarg.h
 * @brief   Variadic argument macros — backed by GCC compiler built-ins.
 *
 * Using __builtin_va_* is the only portable way to implement stdarg.h for
 * GCC.  The manual char-pointer approach breaks with -O2 because the compiler
 * may keep the last named parameter in a register rather than on the stack,
 * making &last a bogus address.  GCC's own built-ins handle this correctly
 * regardless of optimisation level.
 *
 * These built-ins are always available in GCC, even with -nostdinc, because
 * they are part of the compiler itself, not the C standard library.
 */
#ifndef _NOXLIB_STDARG_H
#define _NOXLIB_STDARG_H

typedef __builtin_va_list  va_list;

#define va_start(ap, last)      __builtin_va_start(ap, last)
#define va_arg(ap, type)        __builtin_va_arg(ap, type)
#define va_end(ap)              __builtin_va_end(ap)
#define va_copy(dst, src)       __builtin_va_copy(dst, src)

#endif /* _NOXLIB_STDARG_H */
