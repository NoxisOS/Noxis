/**
 * @file    proc/fd.c
 * @brief   File-descriptor syscalls: open, close, lseek, read, write.
 *
 * Each process owns a small fd table (process_t.fds).  fds 0/1/2 are the
 * console (keyboard / VGA+serial); open() binds further fds to VFS files.
 */
#include <kernel/syscall/syscall.h>
#include <proc/process.h>
#include <proc/scheduler.h>
#include <fs/vfs/vfs.h>
#include <drivers/tty.h>
#include <fs/devfs/devfs.h>
#include <common/types.h>

void    vga_write_buf(const uint8_t* buf, uint32_t len);
void    vga_put_char(uint8_t c);
void    serial_write_n(const uint8_t* buf, uint32_t len);
int32_t kbd_poll(void);

/* pipe ops (proc/pipe.c) */
void    pipe_addref(int kind, void* file);
void    pipe_close(int kind, void* file);
int64_t pipe_read(void* file, uint8_t* buf, uint64_t len);
int64_t pipe_write(void* file, const uint8_t* buf, uint64_t len);

static int fd_is_pipe(int kind) { return kind == FD_PIPE_R || kind == FD_PIPE_W; }

/* open() flags (subset of POSIX). */
#define O_RDONLY  0x00
#define O_WRONLY  0x01
#define O_RDWR    0x02
#define O_CREAT   0x40
#define O_TRUNC   0x200

/* lseek() whence. */
#define SEEK_SET  0
#define SEEK_CUR  1
#define SEEK_END  2

static fd_t* fd_get(int fd) {
    if (fd < 0 || fd >= PROC_MAX_FDS) return NULL;
    fd_t* e = &scheduler_current()->fds[fd];
    return e->kind == FD_CLOSED ? NULL : e;
}

/* Map a DEV_*_INO to the appropriate fd kind; return -1 if not a dev file. */
static int _dev_kind(uint32_t ino) {
    switch (ino) {
    case DEV_NULL_INO:   return FD_DEV_NULL;
    case DEV_ZERO_INO:   return FD_DEV_ZERO;
    case DEV_TTY_INO:    return FD_DEV_TTY;
    case DEV_RANDOM_INO: return FD_DEV_RANDOM;
    default:             return -1;
    }
}

/* Simple LCG for /dev/random (seeded lazily from PIT uptime). */
static uint32_t g_rand;
extern uint32_t pit_uptime_ms(void);
static uint32_t _rand_next(void) {
    if (!g_rand) {
        g_rand = pit_uptime_ms() ^ 0xDEAD1337u;
        if (!g_rand) g_rand = 1;
    }
    g_rand = g_rand * 1664525u + 1013904223u;
    return g_rand;
}

int64_t sys_open(const uint8_t* path, int flags) {
    process_t* p = scheduler_current();
    vfs_file_t* f = vfs_lookup_at(p->cwd_ino, path);
    if (!f && (flags & O_CREAT)) f = vfs_creat(path);
    if (!f) return -1;
    if (flags & O_TRUNC) f->size = 0;

    for (int fd = 3; fd < PROC_MAX_FDS; fd++) {
        if (p->fds[fd].kind != FD_CLOSED) continue;
        int dk = _dev_kind(f->inode);
        if (dk >= 0) {
            p->fds[fd].kind   = dk;
            p->fds[fd].file   = (void*)0;
            p->fds[fd].offset = 0;
        } else {
            p->fds[fd].kind   = FD_FILE;
            p->fds[fd].file   = f;
            p->fds[fd].offset = 0;
        }
        return fd;
    }
    return -1;                          /* table full */
}

int64_t sys_close(int fd) {
    fd_t* e = fd_get(fd);
    if (!e) return -1;
    if (fd_is_pipe(e->kind)) pipe_close(e->kind, e->file);
    e->kind = FD_CLOSED; e->file = NULL; e->offset = 0;
    return 0;
}

int64_t sys_dup(int fd) {
    fd_t* e = fd_get(fd);
    if (!e) return -1;
    process_t* p = scheduler_current();
    for (int i = 0; i < PROC_MAX_FDS; i++)
        if (p->fds[i].kind == FD_CLOSED) {
            p->fds[i] = *e;
            if (fd_is_pipe(e->kind)) pipe_addref(e->kind, e->file);
            return i;
        }
    return -1;
}

int64_t sys_dup2(int oldfd, int newfd) {
    fd_t* e = fd_get(oldfd);
    if (!e) return -1;
    if (newfd < 0 || newfd >= PROC_MAX_FDS) return -1;
    if (oldfd == newfd) return newfd;
    process_t* p = scheduler_current();
    if (fd_is_pipe(p->fds[newfd].kind)) pipe_close(p->fds[newfd].kind, p->fds[newfd].file);
    p->fds[newfd] = *e;
    if (fd_is_pipe(e->kind)) pipe_addref(e->kind, e->file);
    return newfd;
}

int64_t sys_lseek(int fd, int64_t off, int whence) {
    fd_t* e = fd_get(fd);
    if (!e || e->kind != FD_FILE) return -1;
    vfs_file_t* f = (vfs_file_t*)e->file;
    int64_t base = (whence == SEEK_CUR) ? (int64_t)e->offset
                 : (whence == SEEK_END) ? (int64_t)f->size : 0;
    int64_t pos = base + off;
    if (pos < 0) return -1;
    e->offset = (uint32_t)pos;
    return pos;
}

int64_t sys_read(int fd, uint8_t* buf, uint64_t len) {
    fd_t* e = fd_get(fd);
    if (!e) return -1;

    /* ── /dev device reads ── */
    if (e->kind == FD_DEV_NULL)   return 0;   /* EOF */
    if (e->kind == FD_DEV_ZERO)   { for (uint64_t i=0;i<len;i++) buf[i]=0; return (int64_t)len; }
    if (e->kind == FD_DEV_RANDOM) {
        for (uint64_t i = 0; i < len; i++) {
            if ((i & 3) == 0) { uint32_t r = _rand_next(); *(uint32_t*)&buf[i & ~3u] = r; }
        }
        return (int64_t)len;
    }
    if (e->kind == FD_DEV_TTY)    e = &scheduler_current()->fds[0]; /* reroute to stdin */

    if (e->kind == FD_CON_IN) {
        uint64_t got = 0;

        if (tty_canonical()) {
            /* ── Canonical (line-buffered) mode ── */
            while (got < len) {
                int32_t c;
                __asm__ __volatile__("sti");
                while ((c = kbd_poll()) < 0) __asm__ __volatile__("hlt");

                if (c == '\b' || c == 0x7F) {
                    if (got > 0) {
                        got--;
                        if (tty_echo())
                            { vga_put_char('\b'); vga_put_char(' '); vga_put_char('\b'); }
                    }
                    continue;
                }
                if (c == '\r') c = '\n';
                if (tty_echo()) vga_put_char((uint8_t)c);
                buf[got++] = (uint8_t)c;
                if (c == '\n') break;
            }
        } else {
            /* ── Raw mode: deliver chars immediately ── */
            uint8_t vmin = g_termios.c_cc[TTY_VMIN];
            if (!vmin) vmin = 1;

            __asm__ __volatile__("sti");
            while (got < len) {
                int32_t c = kbd_poll();
                if (c < 0) {
                    if (got >= vmin) break;   /* enough chars: return now */
                    __asm__ __volatile__("hlt");
                    continue;
                }
                if (tty_echo()) vga_put_char((uint8_t)c);
                buf[got++] = (uint8_t)c;
                if (got >= vmin) break;
            }
        }

        __asm__ __volatile__("cli");
        return (int64_t)got;
    }

    if (e->kind == FD_FILE) {          /* copy from the file's backing buffer */
        vfs_file_t* f = (vfs_file_t*)e->file;
        uint64_t got = 0;
        while (got < len && e->offset < f->size)
            buf[got++] = f->data[e->offset++];
        return (int64_t)got;
    }
    if (e->kind == FD_PIPE_R) return pipe_read(e->file, buf, len);
    return -1;                          /* not readable */
}

int64_t sys_write(int fd, const uint8_t* buf, uint64_t len) {
    fd_t* e = fd_get(fd);
    if (!e) return -1;

    /* ── /dev device writes ── */
    if (e->kind == FD_DEV_NULL || e->kind == FD_DEV_ZERO || e->kind == FD_DEV_RANDOM)
        return (int64_t)len;   /* discard */
    if (e->kind == FD_DEV_TTY) e = &scheduler_current()->fds[1]; /* reroute to stdout */

    if (e->kind == FD_CON_OUT) {
        vga_write_buf(buf, (uint32_t)len);
        serial_write_n(buf, (uint32_t)len);
        return (int64_t)len;
    }

    if (e->kind == FD_FILE) {
        vfs_file_t* f = (vfs_file_t*)e->file;
        int32_t w = vfs_write_file(f, e->offset, buf, (uint32_t)len);
        if (w > 0) e->offset += (uint32_t)w;
        return w;
    }
    if (e->kind == FD_PIPE_W) return pipe_write(e->file, buf, len);
    return -1;                          /* not writable */
}
