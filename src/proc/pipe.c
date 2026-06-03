/**
 * @file    proc/pipe.c
 * @brief   Anonymous pipes — a fixed pool of ring buffers shared via fds.
 *
 * A pipe has separate reader/writer open counts.  read() blocks (yielding)
 * while empty and writers remain; returns 0 (EOF) once all writers close.
 * write() blocks while full and readers remain.
 */
#include <kernel/syscall/syscall.h>
#include <proc/process.h>
#include <proc/scheduler.h>
#include <common/types.h>

#define PIPE_CAP   4096
#define NPIPES     16

typedef struct {
    uint8_t  buf[PIPE_CAP];
    uint32_t head, tail, count;
    int      readers, writers;
    int      used;
} pipe_t;

static pipe_t g_pipes[NPIPES];

/* Create a pipe; fd[0]=read end, fd[1]=write end. Returns 0, or -1. */
int64_t sys_pipe(int* fd) {
    process_t* p = scheduler_current();
    int rfd = -1, wfd = -1;
    for (int i = 0; i < PROC_MAX_FDS; i++) {
        if (p->fds[i].kind != FD_CLOSED) continue;
        if (rfd < 0) rfd = i; else { wfd = i; break; }
    }
    if (rfd < 0 || wfd < 0) return -1;

    pipe_t* pp = NULL;
    for (int i = 0; i < NPIPES; i++) if (!g_pipes[i].used) { pp = &g_pipes[i]; break; }
    if (!pp) return -1;

    pp->head = pp->tail = pp->count = 0;
    pp->readers = pp->writers = 1;
    pp->used = 1;

    p->fds[rfd].kind = FD_PIPE_R; p->fds[rfd].file = pp; p->fds[rfd].offset = 0;
    p->fds[wfd].kind = FD_PIPE_W; p->fds[wfd].file = pp; p->fds[wfd].offset = 0;
    fd[0] = rfd; fd[1] = wfd;
    return 0;
}

/* Account a new reference to a pipe end (dup/dup2/fork). */
void pipe_addref(int kind, void* file) {
    pipe_t* pp = (pipe_t*)file;
    if (!pp) return;
    if (kind == FD_PIPE_R) pp->readers++;
    else if (kind == FD_PIPE_W) pp->writers++;
}

/* Drop a reference; free the pipe once both ends are fully closed. */
void pipe_close(int kind, void* file) {
    pipe_t* pp = (pipe_t*)file;
    if (!pp) return;
    if (kind == FD_PIPE_R && pp->readers > 0) pp->readers--;
    else if (kind == FD_PIPE_W && pp->writers > 0) pp->writers--;
    if (pp->readers == 0 && pp->writers == 0) pp->used = 0;
}

int64_t pipe_read(void* file, uint8_t* buf, uint64_t len) {
    pipe_t* pp = (pipe_t*)file;
    uint64_t got = 0;
    while (got < len) {
        if (pp->count == 0) {
            if (pp->writers == 0) break;       /* EOF: no more writers */
            scheduler_yield();                 /* wait for data */
            continue;
        }
        buf[got++] = pp->buf[pp->head];
        pp->head = (pp->head + 1) % PIPE_CAP;
        pp->count--;
    }
    return (int64_t)got;
}

int64_t pipe_write(void* file, const uint8_t* buf, uint64_t len) {
    pipe_t* pp = (pipe_t*)file;
    uint64_t put = 0;
    while (put < len) {
        if (pp->readers == 0) return (put > 0) ? (int64_t)put : -1;  /* broken pipe */
        if (pp->count == PIPE_CAP) { scheduler_yield(); continue; }  /* full: wait */
        pp->buf[pp->tail] = buf[put++];
        pp->tail = (pp->tail + 1) % PIPE_CAP;
        pp->count++;
    }
    return (int64_t)put;
}
