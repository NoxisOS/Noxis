/**
 * @file    noxlib/include/stddef.h
 * @brief   size_t, ptrdiff_t, NULL, offsetof
 */
#ifndef _NOXLIB_STDDEF_H
#define _NOXLIB_STDDEF_H

#include <stdint.h>

typedef uint32_t  size_t;
typedef int32_t   ssize_t;
typedef int32_t   ptrdiff_t;

#define NULL  ((void*)0)

#define offsetof(type, member)  ((size_t)((char*)&((type*)0)->member - (char*)0))

#endif /* _NOXLIB_STDDEF_H */
