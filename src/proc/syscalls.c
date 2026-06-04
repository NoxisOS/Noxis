/**
 * @file    proc/syscalls.c
 * @brief   Process-management syscalls: fork, exec, exit, getpid, waitpid.
 */
#include <kernel/syscall/syscall.h>
#include <proc/process.h>
#include <proc/scheduler.h>
#include <mm/virt/vmm.h>
#include <mm/phys/pmm.h>
#include <mm/virt/heap.h>
#include <mm/virt/uvm.h>
#include <fs/vfs/vfs.h>
#include <fs/noxfs/noxfs.h>
#include <common/types.h>

void serial_write(const uint8_t* s);
void serial_write_hex64(uint64_t v);
uint64_t elf64_load_into(uint64_t pml4_phys, const uint8_t* img, uint64_t* brk_out);
void pipe_addref(int kind, void* file);
void pipe_close(int kind, void* file);
static int fd_is_pipe(int k) { return k == FD_PIPE_R || k == FD_PIPE_W; }

/* Stack layout comes from uvm.h (USTACK_BASE / USTACK_TOP / USTACK_LIMIT). */
#define MAX_ARGS      16

static uint64_t kstrlen(const uint8_t* s) { uint64_t n = 0; while (s[n]) n++; return n; }

/* ── fork ──────────────────────────────────────────────────────
 * Duplicate the current process: copy its user address space, then build a
 * child kernel stack that, when first scheduled, `ret`s into fork_ret_trampoline
 * and sysrets to ring 3 with a cloned register frame (rax = 0).
 */
int64_t sys_fork(syscall_frame_t* f) {
    process_t* parent = scheduler_current();

    uint64_t child_pml4 = vmm_create_address_space();
    if (!child_pml4) return -1;
    if (vmm_cow_user_space(child_pml4, parent->pml4) != 0) return -1;

    process_t* child = proc_alloc(parent->name);
    if (!child) return -1;
    child->pml4      = child_pml4;
    child->parent    = parent;
    child->stack_low = parent->stack_low;
    child->cwd_ino   = parent->cwd_ino;
    child->brk       = parent->brk;
    for (int i = 0; i < 128; i++) child->cwd_path[i] = parent->cwd_path[i];
    for (int i = 0; i < PROC_MAX_FDS; i++) {
        child->fds[i] = parent->fds[i];
        if (fd_is_pipe(child->fds[i].kind))      /* extra reference per end */
            pipe_addref(child->fds[i].kind, child->fds[i].file);
    }
    child->sig_pending = 0;                      /* pending signals are not inherited */
    for (int i = 0; i < 32; i++) child->sig_handler[i] = parent->sig_handler[i];

    /* Cloned syscall frame at the top of the child's kernel stack. */
    syscall_frame_t* cf =
        (syscall_frame_t*)(child->kstack_top - sizeof(syscall_frame_t));
    *cf = *f;
    cf->rax = 0;                       /* the child sees fork() return 0 */

    /* Below it: a kthread_switch save area whose `ret` lands in the trampoline.
       kthread_switch pops r15,r14,r13,r12,rbp,rbx then ret → memory order
       (low→high): r15..rbx, ret. */
    uint64_t* sp = (uint64_t*)cf;
    *--sp = (uint64_t)fork_ret_trampoline;   /* ret target */
    *--sp = 0;   /* rbx */
    *--sp = 0;   /* rbp */
    *--sp = 0;   /* r12 */
    *--sp = 0;   /* r13 */
    *--sp = 0;   /* r14 */
    *--sp = 0;   /* r15 */
    child->kctx_rsp = (uint64_t)sp;

    scheduler_register(child);
    return (int64_t)child->pid;        /* parent gets the child's pid */
}

/* ── exec ──────────────────────────────────────────────────────
 * Replace the current process's image with the program at `path`. Builds a
 * fresh address space, loads the ELF, then rewrites the syscall frame so the
 * return path sysrets straight into the new program.
 */
int64_t sys_exec(syscall_frame_t* f, const uint8_t* path, const uint8_t** argv) {
    vfs_file_t* prog = vfs_lookup(path);
    if (!prog || !prog->data) return -1;

    uint64_t nas = vmm_create_address_space();
    if (!nas) return -1;
    uint64_t brk_init = 0;
    uint64_t entry = elf64_load_into(nas, prog->data, &brk_init);
    if (!entry) return -1;

    uint64_t stk = pmm_alloc_frame();
    if (!stk) return -1;
    vmm_map_page_into(nas, USTACK_BASE, stk, PAGE_RW | PAGE_USER);

    /* Build the argv stack into the new page through the physmap (the physmap
       is shared, so argv from the *current* AS is still readable here — we have
       not switched CR3 yet). Layout at rsp: argc, argv[0..argc-1], NULL, strings. */
    uint8_t*  page = (uint8_t*)(PHYSMAP_BASE + stk);
    uint64_t  off  = 0x1000;
    uint64_t  argv_uva[MAX_ARGS];
    int       argc = 0;
    if (argv) for (; argc < MAX_ARGS && argv[argc]; argc++) { /* count */ }

    for (int i = argc - 1; i >= 0; i--) {               /* copy strings, top-down */
        uint64_t len = kstrlen(argv[i]) + 1;
        off -= len;
        for (uint64_t b = 0; b < len; b++) page[off + b] = argv[i][b];
        argv_uva[i] = USTACK_BASE + off;
    }
    off &= ~7ULL;                                       /* align */
    off -= 8; *(uint64_t*)(page + off) = 0;             /* argv[argc] = NULL */
    for (int i = argc - 1; i >= 0; i--) { off -= 8; *(uint64_t*)(page + off) = argv_uva[i]; }
    off -= 8; *(uint64_t*)(page + off) = (uint64_t)argc;/* argc (rsp points here) */

    process_t* cur = scheduler_current();
    uint64_t   old_pml4 = cur->pml4;
    cur->pml4 = nas;
    vmm_switch(nas);                   /* kernel half is shared, frame stays valid */
    vmm_free_user_space(old_pml4);     /* reclaim old pages now that we've switched */

    cur->stack_low = USTACK_BASE;
    cur->brk = brk_init;               /* heap starts right after the ELF image */
    if (cur->cwd_ino == 0) {           /* first exec: inherit root as cwd */
        cur->cwd_ino = vfs_root_ino();
        cur->cwd_path[0] = '/'; cur->cwd_path[1] = 0;
    }
    f->rip    = entry;
    f->ursp   = USTACK_BASE + off;
    f->rflags = 0x202;                 /* IF=1 */
    f->rax    = 0;
    return 0;
}

void sys_exit(int code) {
    /* Release any pipe references so readers see EOF / writers see broken pipe. */
    process_t* cur = scheduler_current();
    for (int i = 0; i < PROC_MAX_FDS; i++)
        if (fd_is_pipe(cur->fds[i].kind)) pipe_close(cur->fds[i].kind, cur->fds[i].file);

    serial_write((const uint8_t*)"[noxis64] pid ");
    serial_write_hex64(scheduler_current()->pid);
    serial_write((const uint8_t*)" exit code=");
    serial_write_hex64((uint64_t)(uint32_t)code);
    serial_write((const uint8_t*)"\n");
    scheduler_exit(code);              /* never returns */
}

uint64_t sys_getpid(void) { return scheduler_current()->pid; }

/* setfg(pid): mark pid as the foreground process (receives Ctrl-C SIGINT). */
void sys_setfg(uint64_t pid) { scheduler_set_fg(pid); }

/* brk(addr): set the program break to addr (grows the heap).
 * addr == 0 → return current break without changing it.
 * Maps new pages as needed; returns the new break, or the old one on failure. */
int64_t sys_brk(uint64_t addr) {
    process_t* p = scheduler_current();
    if (addr == 0) return (int64_t)p->brk;
    if (addr <= p->brk) return (int64_t)p->brk;   /* no shrink */
    if (addr >= USER_HEAP_MAX) return (int64_t)p->brk;

    /* Map every new page between old break (page-aligned up) and new break. */
    uint64_t old_top = (p->brk  + 0xFFFULL) & ~0xFFFULL;
    uint64_t new_top = (addr    + 0xFFFULL) & ~0xFFFULL;
    for (uint64_t va = old_top; va < new_top; va += 0x1000) {
        uint64_t frame = pmm_alloc_frame();
        if (!frame) return (int64_t)p->brk;   /* OOM: return unchanged break */
        uint8_t* pg = (uint8_t*)(PHYSMAP_BASE + frame);
        for (int i = 0; i < 4096; i++) pg[i] = 0;
        vmm_map_page_into(p->pml4, va, frame, PAGE_RW | PAGE_USER);
    }
    p->brk = addr;
    return (int64_t)addr;
}

/* ── Path helpers ──────────────────────────────────────────────────────── */

/* Split path into parent directory + final component.
 *   "foo"        → parent="."  name="foo"
 *   "a/b/c"      → parent="a/b"  name="c"
 *   "/a/b"       → parent="/"  name="b"
 *   "/foo"       → parent="/"  name="foo"
 */
static void path_split(const uint8_t* path,
                        uint8_t parent[128], uint8_t name[32]) {
    int len = 0;
    while (path[len]) len++;

    int slash = -1;
    for (int i = len - 1; i >= 0; i--) {
        if (path[i] == '/') { slash = i; break; }
    }

    if (slash < 0) {
        parent[0] = '.'; parent[1] = 0;
        int i = 0;
        while (i < 31 && path[i]) { name[i] = path[i]; i++; }
        name[i] = 0;
    } else if (slash == 0) {
        parent[0] = '/'; parent[1] = 0;
        int i = 0;
        while (i < 31 && path[slash + 1 + i]) { name[i] = path[slash+1+i]; i++; }
        name[i] = 0;
    } else {
        int i = 0;
        while (i < 127 && i < slash) { parent[i] = path[i]; i++; }
        parent[i] = 0;
        i = 0;
        while (i < 31 && path[slash + 1 + i]) { name[i] = path[slash+1+i]; i++; }
        name[i] = 0;
    }
}

/* Resolve `rel` against `base` into `dst` (max dst_size bytes), normalising
 * `.` / `..` and double slashes.  Result always starts with `/`. */
static void path_resolve_str(uint8_t* dst, uint32_t dst_size,
                              const uint8_t* base, const uint8_t* rel) {
    uint8_t tmp[256];
    uint32_t ti = 0;

    if (rel[0] == '/') {
        while (ti < 255 && rel[ti]) { tmp[ti] = rel[ti]; ti++; }
    } else {
        uint32_t bi = 0;
        while (bi < 255 && base[bi]) { tmp[ti++] = base[bi++]; }
        if (ti > 0 && tmp[ti - 1] != '/') tmp[ti++] = '/';
        uint32_t ri = 0;
        while (ti < 255 && rel[ri]) { tmp[ti++] = rel[ri++]; }
    }
    tmp[ti] = 0;

    /* Tokenise into component stack. */
    uint8_t  comps[16][32];
    uint32_t clen[16];
    int      ncomp = 0;
    uint8_t* p = tmp;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        uint8_t* start = p;
        while (*p && *p != '/') p++;
        uint32_t len = (uint32_t)(p - start);
        if (!len) continue;
        if (len == 1 && start[0] == '.') continue;
        if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (ncomp > 0) ncomp--;
            continue;
        }
        if (ncomp < 16 && len < 32) {
            for (uint32_t i = 0; i < len; i++) comps[ncomp][i] = start[i];
            comps[ncomp][len] = 0;
            clen[ncomp] = len;
            ncomp++;
        }
    }

    /* Reconstruct canonical path. */
    uint32_t di = 0;
    dst[di++] = '/';
    for (int i = 0; i < ncomp && di < dst_size - 2; i++) {
        if (i > 0) dst[di++] = '/';
        for (uint32_t j = 0; j < clen[i] && di < dst_size - 1; j++)
            dst[di++] = comps[i][j];
    }
    dst[di] = 0;
}

/* ── cwd syscalls ──────────────────────────────────────────────────────── */

int64_t sys_chdir(const uint8_t* path) {
    if (!path || !path[0]) return -1;
    process_t* p = scheduler_current();
    uint32_t new_ino = vfs_resolve_ino(p->cwd_ino, path);
    if (new_ino == (uint32_t)-1) return -1;
    if (!vfs_is_dir(new_ino)) return -1;
    p->cwd_ino = new_ino;
    path_resolve_str(p->cwd_path, 128, p->cwd_path, path);
    return 0;
}

int64_t sys_getcwd(uint8_t* buf, uint64_t size) {
    if (!buf || size == 0) return -1;
    process_t* p = scheduler_current();
    uint64_t i = 0;
    while (i < size - 1 && p->cwd_path[i]) { buf[i] = p->cwd_path[i]; i++; }
    buf[i] = 0;
    return (int64_t)i;
}

/* stat buffer layout (16 bytes, mirrors nox_stat_t in noxlib.h). */
typedef struct { uint32_t ino; uint16_t mode; uint16_t _p; uint32_t size; uint32_t _p2; } nox_stat_t;

int64_t sys_mkdir(const uint8_t* path) {
    if (!path || !path[0]) return -1;
    process_t* p = scheduler_current();
    uint8_t par[128], name[32];
    path_split(path, par, name);
    uint32_t par_ino = vfs_resolve_ino(p->cwd_ino, par);
    if (par_ino == (uint32_t)-1 || !vfs_is_dir(par_ino)) return -1;
    return (noxfs_mkdir(par_ino, name) == (uint32_t)-1) ? -1 : 0;
}

int64_t sys_unlink(const uint8_t* path) {
    if (!path || !path[0]) return -1;
    process_t* p = scheduler_current();
    uint8_t par[128], name[32];
    path_split(path, par, name);
    uint32_t par_ino = vfs_resolve_ino(p->cwd_ino, par);
    if (par_ino == (uint32_t)-1) return -1;
    /* Try file first, then empty directory */
    if (noxfs_unlink(par_ino, name) == OS_OK) return 0;
    return (noxfs_rmdir(par_ino, name) == OS_OK) ? 0 : -1;
}

int64_t sys_stat(const uint8_t* path, nox_stat_t* buf) {
    if (!path || !buf) return -1;
    process_t* p = scheduler_current();
    uint32_t ino = vfs_resolve_ino(p->cwd_ino, path);
    if (ino == (uint32_t)-1) return -1;
    vfs_file_t st;
    if (noxfs_stat(ino, &st) != OS_OK) return -1;
    buf->ino  = ino;
    buf->mode = (uint16_t)st.capacity;   /* noxfs_stat stores mode in capacity */
    buf->size = st.size;
    buf->_p   = 0; buf->_p2 = 0;
    return 0;
}

int64_t sys_rename(const uint8_t* old_path, const uint8_t* new_path) {
    if (!old_path || !new_path) return -1;
    process_t* p = scheduler_current();
    uint8_t opar[128], oname[32], npar[128], nname[32];
    path_split(old_path, opar, oname);
    path_split(new_path, npar, nname);
    uint32_t oino = vfs_resolve_ino(p->cwd_ino, opar);
    uint32_t nino = vfs_resolve_ino(p->cwd_ino, npar);
    if (oino == (uint32_t)-1 || nino == (uint32_t)-1) return -1;
    return (noxfs_rename(oino, oname, nino, nname) == OS_OK) ? 0 : -1;
}

int64_t sys_getdents(const uint8_t* path, uint8_t* buf, uint64_t len) {
    if (!buf || len == 0) return -1;
    process_t* p = scheduler_current();
    uint32_t dir_ino = vfs_resolve_ino(p->cwd_ino,
                                       (path && path[0]) ? path
                                                         : (const uint8_t*)".");
    if (dir_ino == (uint32_t)-1) return -1;
    if (!vfs_is_dir(dir_ino)) return -1;
    uint32_t off = 0;
    return (int64_t)vfs_getdents(dir_ino, buf, (uint32_t)len, &off);
}

/* Copy the name of the idx-th VFS entry into namebuf (<=32). Returns 1 if it
   exists, 0 past the end — lets userland `ls` enumerate the directory. */
/* procinfo(idx, buf): fill { int64 pid; int64 state; char name[32] } for the
   idx-th process. Returns 1 if it exists, 0 past the end — the /proc payoff. */
int64_t sys_procinfo(uint64_t idx, uint8_t* buf) {
    process_t* p = scheduler_at((uint32_t)idx);
    if (!p) return 0;
    int64_t*  q = (int64_t*)buf;
    q[0] = (int64_t)p->pid;
    q[1] = (int64_t)p->state;
    uint8_t* nm = buf + 16;
    int i = 0; for (; p->name[i] && i < 31; i++) nm[i] = p->name[i]; nm[i] = 0;
    return 1;
}

int64_t sys_readdir(uint64_t idx, uint8_t* namebuf) {
    if (idx >= vfs_count()) return 0;
    vfs_file_t* f = vfs_entry((uint32_t)idx);
    if (!f) return 0;
    int i = 0;
    for (; f->name[i] && i < 31; i++) namebuf[i] = f->name[i];
    namebuf[i] = 0;
    return 1;
}

int64_t sys_waitpid(int64_t pid, int* status) {
    process_t* parent = scheduler_current();
    for (;;) {
        process_t* z = scheduler_reap(parent, pid);
        if (z) {
            if (status) *status = z->exit_code;
            int64_t cpid = (int64_t)z->pid;
            scheduler_remove(z);
            vmm_free_user_space(z->pml4);      /* release all user pages */
            kfree((void*)z->kstack_base);      /* kernel stack */
            kfree(z);                          /* process struct */
            return cpid;
        }
        scheduler_yield();             /* let the child run, then re-check */
    }
}
