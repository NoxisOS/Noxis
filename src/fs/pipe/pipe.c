/**
 * @file    fs/pipe.c
 * @brief   Pipe implementation — kernel ring buffer with blocking I/O.
 *          Uses scheduler_block_on / scheduler_wake for wait channels.
 * @author  Noxis Team
 * @date    2026-05-30
 */
#include <fs/pipe/pipe.h>
#include <proc/scheduler.h>
#include <mm/virt/heap.h>
#include <kernel/hal/ports.h>
#include <common/types.h>

pipe_t* pipe_alloc(void) {
    pipe_t* p = (pipe_t*)kmalloc(sizeof(pipe_t));
    if (!p) return (pipe_t*)0;

    p->head   = 0;
    p->tail   = 0;
    p->count  = 0;
    p->reader = (process_t*)0;
    p->writer = (process_t*)0;
    p->refs   = 2;  /* one for each fd */
    return p;
}

int32_t pipe_read(pipe_t* p, uint8_t* buf, uint32_t len) {
    if (!p || !buf || len == 0) return 0;

    /* Wait for data or EOF. */
    while (p->count == 0) {
        if (p->refs < 2) {
            /* No writers left — EOF.  p->refs=1 means only reader is open. */
            return 0;
        }
        /* Block: yield to scheduler until writer wakes us. */
        p->reader = scheduler_current();
        scheduler_block_on((process_t**)&p->reader);
    }

    __asm__ __volatile__("cli");
    uint32_t to_copy = len < p->count ? len : p->count;
    for (uint32_t i = 0; i < to_copy; i++) {
        buf[i] = p->buf[p->tail];
        p->tail = (p->tail + 1) % PIPE_SIZE;
    }
    p->count -= to_copy;

    /* Wake a blocked writer if we made room. */
    if (p->writer) {
        process_t* w = p->writer;
        p->writer = (process_t*)0;
        scheduler_wake((process_t**)&w);
    }
    __asm__ __volatile__("sti");
    return (int32_t)to_copy;
}

int32_t pipe_write(pipe_t* p, const uint8_t* buf, uint32_t len) {
    if (!p || !buf || len == 0) return 0;

    /* No reader → broken pipe. */
    if (p->refs < 2) return -1;

    uint32_t written = 0;
    while (written < len) {
        /* Wait for space. */
        while (p->count >= PIPE_SIZE) {
            if (p->refs < 2) return -1;  /* reader gone */
            p->writer = scheduler_current();
            scheduler_block_on((process_t**)&p->writer);
        }

        __asm__ __volatile__("cli");
        uint32_t space = PIPE_SIZE - p->count;
        uint32_t chunk = (len - written) < space ? (len - written) : space;
        for (uint32_t i = 0; i < chunk; i++) {
            p->buf[p->head] = buf[written + i];
            p->head = (p->head + 1) % PIPE_SIZE;
        }
        p->count += chunk;
        written  += chunk;

        /* Wake blocked reader. */
        if (p->reader) {
            process_t* r = p->reader;
            p->reader = (process_t*)0;
            scheduler_wake((process_t**)&r);
        }
        __asm__ __volatile__("sti");
    }
    return (int32_t)written;
}

void pipe_close(pipe_t* p) {
    if (!p || p->refs <= 0) return;

    __asm__ __volatile__("cli");
    p->refs--;

    /* Wake the blocked opposite end so it sees EOF/broken pipe. */
    if (p->reader && p->refs < 2) {
        process_t* r = p->reader;
        p->reader = (process_t*)0;
        scheduler_wake((process_t**)&r);
    }
    if (p->writer) {
        process_t* w = p->writer;
        p->writer = (process_t*)0;
        scheduler_wake((process_t**)&w);
    }
    __asm__ __volatile__("sti");

    if (p->refs == 0)
        kfree(p);
}
