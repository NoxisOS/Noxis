/**
 * @file    fs/pipe.h
 * @brief   Anonymous pipe — kernel ring buffer for IPC.
 *          reader blocks when empty, writer blocks when full.
 * @author  Noxis Team
 * @date    2026-05-30
 */
#ifndef FS_PIPE_H
#define FS_PIPE_H

#include <common/types.h>
#include <proc/process.h>

#define PIPE_SIZE    4096

typedef struct pipe {
    uint8_t    buf[PIPE_SIZE];
    uint32_t   head;       /* write position */
    uint32_t   tail;       /* read position  */
    uint32_t   count;      /* bytes available */
    process_t* reader;     /* blocked reader (or NULL) */
    process_t* writer;     /* blocked writer (or NULL) */
    int        refs;       /* open fd count (0 → free) */
} pipe_t;

/**
 * @brief Allocate a pipe.  Both reader and writer fds should point to it.
 */
pipe_t* pipe_alloc(void);

/**
 * @brief Read up to `len` bytes from the pipe.  Blocks if empty.
 * @return bytes read, or 0 on EOF (all writers gone), or -1 on error.
 */
int32_t pipe_read(pipe_t* p, uint8_t* buf, uint32_t len);

/**
 * @brief Write up to `len` bytes into the pipe.  Blocks if full.
 * @return bytes written, or -1 on error (no readers left → EPIPE).
 */
int32_t pipe_write(pipe_t* p, const uint8_t* buf, uint32_t len);

/**
 * @brief Close one end of the pipe.  Decrements refcount; wakes the
 *        opposite end if it was blocked.
 */
void    pipe_close(pipe_t* p);

#endif
