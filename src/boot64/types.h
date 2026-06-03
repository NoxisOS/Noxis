/**
 * @file    src/boot64/types.h
 * @brief   Freestanding integer types for the 64-bit kernel (no libc).
 */
#ifndef B64_TYPES_H
#define B64_TYPES_H

typedef unsigned char       uint8_t;
typedef unsigned short      uint16_t;
typedef unsigned int        uint32_t;
typedef unsigned long long  uint64_t;
typedef signed   char       int8_t;
typedef signed   short      int16_t;
typedef signed   int        int32_t;
typedef signed   long long  int64_t;
typedef uint64_t            size_t;
typedef uint64_t            uintptr_t;

#define NULL ((void*)0)

/* ── Port I/O ─────────────────────────────────────────────────── */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__("outb %0, %1" :: "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t r;
    __asm__ __volatile__("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

#endif /* B64_TYPES_H */
