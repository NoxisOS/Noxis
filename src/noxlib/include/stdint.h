/**
 * @file    noxlib/include/stdint.h
 * @brief   Fixed-width integer types for Noxis userland
 */
#ifndef _NOXLIB_STDINT_H
#define _NOXLIB_STDINT_H

typedef unsigned char       uint8_t;
typedef unsigned short      uint16_t;
typedef unsigned int        uint32_t;
typedef signed char         int8_t;
typedef signed short        int16_t;
typedef signed int          int32_t;
typedef unsigned int        uintptr_t;
typedef signed int          intptr_t;

#define UINT8_MAX    0xFFu
#define UINT16_MAX   0xFFFFu
#define UINT32_MAX   0xFFFFFFFFu
#define INT8_MAX     0x7F
#define INT16_MAX    0x7FFF
#define INT32_MAX    0x7FFFFFFF
#define INT8_MIN     (-0x80)
#define INT16_MIN    (-0x8000)
#define INT32_MIN    (-0x80000000)

#endif /* _NOXLIB_STDINT_H */
