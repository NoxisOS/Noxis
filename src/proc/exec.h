/**
 * @file    proc/exec.h
 * @brief   ELF execution wrapper — loads an ELF, jumps to ring 3,
 *          and provides a return path via sys_exit.
 * @author  Noxis Team
 * @date    2026-05-30
 */
#ifndef PROC_EXEC_H
#define PROC_EXEC_H

#include <common/types.h>
#include <common/status.h>

/**
 * @brief Load and run an ELF in ring 3. Blocks until the user calls
 *        sys_exit (which longjmps back into this function).
 *
 * Stack layout seen by the user's _start:
 *   [esp+0]      argc
 *   [esp+4]      argv[0]   ← pointer to first string
 *   [esp+8]      argv[1]
 *   ...
 *   [esp+4*argc] NULL
 *   (above)      argv strings, null-terminated
 *
 * @param argc     number of argv entries (argv[0] is the program name)
 * @param argv     argc pointers to null-terminated strings (kernel memory)
 */
os_status_t exec_run(const uint8_t* elf, uint32_t size,
                     uint32_t argc, const uint8_t* const* argv,
                     int* exit_code_out);

/**
 * @brief Called by sys_exit. Longjmps back to exec_run().
 *        Halts the CPU if no exec is currently in progress.
 */
void exec_return(int code) __attribute__((noreturn));

/**
 * @brief Returns the physical address of the currently-active exec PD,
 *        or 0 if no exec is in progress.  Used by sys_fork.
 */
uint32_t exec_current_pd(void);

/**
 * @brief Build the argc/argv frame on the user stack at USER_STACK_TOP.
 *        Used by exec_run and _sys_execve.
 * @param argc  number of arguments
 * @param argv  array of kernel-side pointers to null-terminated strings
 * @return initial user ESP (points at argc on the stack)
 */
uint32_t _build_argv_frame(uint32_t argc, const uint8_t* const* argv);

#endif /* PROC_EXEC_H */
