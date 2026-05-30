/**
 * @file    kernel/hal/fpu.h
 * @brief   x87 FPU support with lazy (TS-trap) state switching.
 * @author  Noxis Team
 * @date    2026-05-30
 */
#ifndef KERNEL_HAL_FPU_H
#define KERNEL_HAL_FPU_H

#include <common/types.h>
#include <common/status.h>

struct process;

/** Size of an FNSAVE/FRSTOR (x87, non-SSE) state image. */
#define FPU_STATE_SIZE  108

/**
 * @brief Enable the x87 FPU and arm lazy context switching.
 *        Clears CR0.EM, sets CR0.MP|NE, runs FNINIT, registers the #NM
 *        (vector 7) handler, then sets CR0.TS so the first FPU use traps.
 */
os_status_t fpu_init(void);

/**
 * @brief Set CR0.TS so the next FPU instruction traps #NM.
 *        Call on every context switch to arm lazy save/restore.
 */
void fpu_set_ts(void);

/**
 * @brief Forget `p` as the current FPU owner (call when it exits) so the
 *        kernel never tries to FNSAVE into a dead process_t.
 */
void fpu_drop_owner(struct process* p);

#endif /* KERNEL_HAL_FPU_H */
