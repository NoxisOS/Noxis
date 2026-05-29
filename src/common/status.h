/**
 * @file    common/status.h
 * @brief   Global operation status codes used throughout the kernel
 * @author  Noxis Team
 * @date    2026-05-29
 */
#ifndef COMMON_STATUS_H
#define COMMON_STATUS_H

/* ── operation status codes ────────────────────────────────── */
typedef enum {
    OS_OK            = 0,  /* Operation succeeded */
    OS_ERR_NULL      = 1,  /* NULL pointer argument */
    OS_ERR_OOM       = 2,  /* Out of memory */
    OS_ERR_INVALID   = 3,  /* Invalid argument */
    OS_ERR_IO        = 4,  /* I/O error */
    OS_ERR_NOT_FOUND = 5,  /* Resource not found */
    OS_ERR_PERM      = 6,  /* Permission denied */
    OS_ERR_BUSY      = 7,  /* Resource busy */
    OS_ERR_RANGE     = 8,  /* Value out of valid range */
    OS_ERR_NOSYS     = 9,  /* Function not implemented */
} os_status_t;

#endif /* COMMON_STATUS_H */
