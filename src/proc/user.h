/**
 * @file    proc/user.h
 * @brief   User mode entry and syscall setup
 * @author  Noxis Team
 * @date    2026-05-30
 */
#ifndef PROC_USER_H
#define PROC_USER_H

#include <common/types.h>
#include <common/status.h>

/**
 * @brief Jumps to ring 3 at the given entry point (never returns)
 * @param entry   User-mode entry point
 * @param stack   User-mode stack pointer
 */
void user_enter(uint32_t entry, uint32_t stack);

/**
 * @brief Initializes the syscall interface (IDT gate 0x80)
 * @return OS_OK on success
 */
os_status_t syscall_init(void);

#endif /* PROC_USER_H */
