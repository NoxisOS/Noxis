/**
 * @file    syscall/syscall.h
 * @brief   System call interface
 * @author  Noxis Team
 * @date    2026-05-30
 */
#ifndef SYSCALL_SYSCALL_H
#define SYSCALL_SYSCALL_H

#include <common/types.h>
#include <common/status.h>

/* ── syscall numbers ───────────────────────────────────────── */
#define SYS_EXIT     0
#define SYS_WRITE    1

/**
 * @brief Initializes the syscall table and registers int 0x80 handler
 * @return OS_OK on success
 */
os_status_t syscall_init(void);

#endif /* SYSCALL_SYSCALL_H */
