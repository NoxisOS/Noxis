/**
 * @file    common/types.h
 * @brief   Fundamental type definitions for the Noxis kernel
 * @author  Noxis Team
 * @date    2026-05-29
 */
#ifndef COMMON_TYPES_H
#define COMMON_TYPES_H

/* ── integer types ─────────────────────────────────────────── */
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;

/* ── boolean ───────────────────────────────────────────────── */
typedef uint8_t  bool_t;

#define TRUE   1
#define FALSE  0

/* ── null pointer ──────────────────────────────────────────── */
#define NULL   ((void*)0)

/* ── aliases for portability ───────────────────────────────── */
typedef uint32_t  phys_addr_t;
typedef uint32_t  virt_addr_t;

#endif /* COMMON_TYPES_H */
