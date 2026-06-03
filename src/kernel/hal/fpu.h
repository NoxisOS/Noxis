/**
 * @file    kernel/hal/fpu.h
 * @brief   x87/SSE FPU bring-up (x86-64).
 * @author  Noxis Team
 */
#ifndef KERNEL_HAL_FPU_H
#define KERNEL_HAL_FPU_H

#include <common/types.h>
#include <common/status.h>

#define FPU_STATE_SIZE  512   /* FXSAVE area */

/* Enable x87 + SSE (long mode always has SSE). Lazy per-process switching
   will be re-added once the scheduler is ported. */
os_status_t fpu_init(void);

#endif /* KERNEL_HAL_FPU_H */
