/**
 * @file    hal/ports.h
 * @brief   x86-64 port I/O — inline implementations (no asm TU needed).
 * @author  Noxis Team
 */
#ifndef HAL_PORTS_H
#define HAL_PORTS_H

#include <common/types.h>

/* ── byte / word / dword port access ───────────────────────── */
static inline uint8_t port_byte_in(uint16_t port) {
    uint8_t r; __asm__ __volatile__("inb %1,%0" : "=a"(r) : "Nd"(port)); return r;
}
static inline void port_byte_out(uint16_t port, uint8_t data) {
    __asm__ __volatile__("outb %0,%1" :: "a"(data), "Nd"(port));
}
static inline uint16_t port_word_in(uint16_t port) {
    uint16_t r; __asm__ __volatile__("inw %1,%0" : "=a"(r) : "Nd"(port)); return r;
}
static inline void port_word_out(uint16_t port, uint16_t data) {
    __asm__ __volatile__("outw %0,%1" :: "a"(data), "Nd"(port));
}
static inline uint32_t port_dword_in(uint16_t port) {
    uint32_t r; __asm__ __volatile__("inl %1,%0" : "=a"(r) : "Nd"(port)); return r;
}
static inline void port_dword_out(uint16_t port, uint32_t data) {
    __asm__ __volatile__("outl %0,%1" :: "a"(data), "Nd"(port));
}
static inline void io_delay(void) {
    __asm__ __volatile__("outb %%al, $0x80" :: "a"(0));
}

/* ── short aliases used by some drivers ─────────────────────── */
static inline uint8_t inb(uint16_t port)            { return port_byte_in(port); }
static inline void    outb(uint16_t port, uint8_t d){ port_byte_out(port, d); }

/* ── CPU control ───────────────────────────────────────────── */
static inline void cpu_sti(void) { __asm__ __volatile__("sti"); }
static inline void cpu_cli(void) { __asm__ __volatile__("cli"); }
static inline void cpu_hlt(void) { __asm__ __volatile__("hlt"); }

#endif /* HAL_PORTS_H */
