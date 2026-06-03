/**
 * @file    common/types.h
 * @brief   Fundamental type definitions for the Noxis kernel
 * @author  Noxis Team
 * @date    2026-05-29
 */
#ifndef COMMON_TYPES_H
#define COMMON_TYPES_H

/* ── integer types ─────────────────────────────────────────── */
typedef unsigned char       uint8_t;
typedef unsigned short      uint16_t;
typedef unsigned int        uint32_t;
typedef unsigned long long  uint64_t;
typedef signed   char       int8_t;
typedef signed   short      int16_t;
typedef signed   int        int32_t;
typedef signed   long long  int64_t;

/* ── size / pointer-width types (x86-64) ───────────────────── */
typedef uint64_t  size_t;
typedef int64_t   ssize_t;
typedef uint64_t  uintptr_t;

/* ── boolean ───────────────────────────────────────────────── */
typedef uint8_t  bool_t;

#define TRUE   1
#define FALSE  0

/* ── null pointer ──────────────────────────────────────────── */
#define NULL   ((void*)0)

/* ── aliases for portability (64-bit physical/virtual) ─────── */
typedef uint64_t  phys_addr_t;
typedef uint64_t  virt_addr_t;

#endif /* COMMON_TYPES_H */
