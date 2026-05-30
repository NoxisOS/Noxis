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
 * @return OS_OK if the user exited cleanly,
 *         OS_ERR_* if the ELF could not be loaded.
 */
os_status_t exec_run(const uint8_t* elf, uint32_t size, int* exit_code_out);

/**
 * @brief Called by sys_exit. Longjmps back to exec_run().
 *        Halts the CPU if no exec is currently in progress.
 */
void exec_return(int code) __attribute__((noreturn));

#endif /* PROC_EXEC_H */
