/**
 * @file    shell/shell.h
 * @brief   Interactive shell — REPL, command registry, shared helpers.
 *          Each builtin lives in its own cmd_*.c file and exposes a
 *          single `const shell_cmd_t cmd_<name>` symbol. The registry
 *          in shell.c collects them via extern declarations.
 * @author  Noxis Team
 * @date    2026-05-30
 */
#ifndef SHELL_SHELL_H
#define SHELL_SHELL_H

#include <common/types.h>

/* ── command interface ──────────────────────────────────────── */

typedef void (*shell_run_fn)(const uint8_t* args);

typedef struct {
    const uint8_t* name;        /* keyword typed at the prompt        */
    const uint8_t* usage;       /* short one-liner shown by `help`    */
    shell_run_fn   run;         /* args = trimmed text after the name */
} shell_cmd_t;

/* ── REPL ──────────────────────────────────────────────────── */

/** Run the shell forever (returns only via the `halt` command). */
void shell_run(void);

/* ── registry access (used by cmd_help) ─────────────────────── */

uint32_t          shell_cmd_count(void);
const shell_cmd_t* shell_cmd_at(uint32_t i);

/* ── shared helpers for commands ────────────────────────────── */

#define SHELL_INDENT  3

/** Common left margin: 3 spaces, current color. */
void  shell_indent(void);

/** "no such file: <name>\n" in red/white. */
void  shell_err_nofile(const uint8_t* name);

/** "usage: <line>\n" in red. */
void  shell_err_usage(const uint8_t* usage);

/** Print a decimal uint32 padded right to `width` cols, value in `fg`. */
void  shell_print_u32(uint32_t v, uint32_t width, uint8_t fg);

/** Trim leading spaces from `s` and return the resulting pointer. */
const uint8_t* shell_skip_spaces(const uint8_t* s);

#endif /* SHELL_SHELL_H */
