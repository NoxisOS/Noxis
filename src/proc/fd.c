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

int64_t sys_open(const uint8_t* path, int flags) {
    process_t* cur = scheduler_current();
    vfs_file_t* f = vfs_lookup_at(cur->cwd_ino, path);
    if (!f && (flags & O_CREAT)) f = vfs_creat(path);
    if (!f) return -1;
    if (flags & O_TRUNC) f->size = 0;

    process_t* p = scheduler_current();
    for (int fd = 3; fd < PROC_MAX_FDS; fd++) {
        if (p->fds[fd].kind == FD_CLOSED) {
            p->fds[fd].kind   = FD_FILE;
            p->fds[fd].file   = f;
            p->fds[fd].offset = 0;
            return fd;
        }
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

    if (e->kind == FD_CON_IN) {        /* canonical line discipline */
        uint64_t got = 0;
        while (got < len) {
            int32_t c;
            __asm__ __volatile__("sti");
            while ((c = kbd_poll()) < 0) __asm__ __volatile__("hlt");

            if (c == '\b' || c == 0x7F) {        /* backspace: erase a char */
                if (got > 0) { got--; vga_put_char('\b'); vga_put_char(' '); vga_put_char('\b'); }
                continue;
            }
            if (c == '\r') c = '\n';
            buf[got++] = (uint8_t)c;
            vga_put_char((uint8_t)c);
            if (c == '\n') break;
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
