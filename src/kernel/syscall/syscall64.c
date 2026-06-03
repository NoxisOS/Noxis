/**
 * @file    kernel/syscall/syscall64.c
 * @brief   x86-64 syscall/sysret setup + dispatcher (port in progress).
 *
 * Minimal during the port: SYS_EXIT and SYS_WRITE only — enough to prove
 * the ring-3 → kernel boundary.  The full table is re-added with the rest
 * of the kernel.
 */
#include <common/types.h>
#include <common/status.h>
#include <drivers/serial.h>
#include <drivers/vga.h>

void serial_write_hex64(uint64_t v);
int32_t kbd_poll(void);

/* ── MSRs ─────────────────────────────────────────────────────── */
#define MSR_EFER    0xC0000080
#define MSR_STAR    0xC0000081
#define MSR_LSTAR   0xC0000082
#define MSR_SFMASK  0xC0000084

extern void syscall_entry(void);   /* syscall_entry.asm */

/* Dedicated kernel stack used by the syscall entry path. */
static uint8_t g_syscall_stack[16384] __attribute__((aligned(16)));
uint64_t g_kernel_rsp = 0;          /* top of the syscall kernel stack */
uint64_t g_user_rsp   = 0;          /* saved user RSP across a syscall  */

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ __volatile__("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}
static inline void wrmsr(uint32_t msr, uint64_t v) {
    __asm__ __volatile__("wrmsr" :: "c"(msr), "a"((uint32_t)v), "d"((uint32_t)(v >> 32)));
}

os_status_t syscall_init(void) {
    g_kernel_rsp = (uint64_t)(g_syscall_stack + sizeof(g_syscall_stack));

    /* STAR: [47:32] = syscall CS (0x08), [63:48] = sysret base (0x10).
       → syscall:  CS=0x08, SS=0x10
       → sysret:   CS=0x10+16=0x20|3, SS=0x10+8=0x18|3 */
    wrmsr(MSR_STAR, ((uint64_t)0x10 << 48) | ((uint64_t)0x08 << 32));
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
    wrmsr(MSR_SFMASK, 0x200);            /* clear IF on entry */

    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | 1);/* EFER.SCE = enable syscall */
    return OS_OK;
}

/* Dispatcher: RAX=num arrives as arg0, then up to 3 args. */
uint64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3) {
    switch (num) {
    case 0:  /* exit(code) */
        serial_write((const uint8_t*)"\n[noxis64] user exit code=");
        serial_write_hex64(a1);
        serial_write((const uint8_t*)"\n");
        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        vga_write((const uint8_t*)"\n[ring-3 process exited]\n");
        for (;;) __asm__ __volatile__("cli; hlt");

    case 1:  /* write(fd, buf, len) */
        (void)a1;
        vga_write_buf((const uint8_t*)a2, (uint32_t)a3);
        serial_write_n((const uint8_t*)a2, (uint32_t)a3);
        return a3;

    case 2: { /* read(fd, buf, len) — line-buffered keyboard read */
        (void)a1;
        uint8_t* buf = (uint8_t*)a2;
        uint64_t got = 0;
        while (got < a3) {
            int32_t c;
            /* The syscall path runs with IF=0 (SFMASK); enable interrupts
               so the keyboard IRQ can fire while we wait. */
            __asm__ __volatile__("sti");
            while ((c = kbd_poll()) < 0) __asm__ __volatile__("hlt");
            if (c == '\r') c = '\n';
            buf[got++] = (uint8_t)c;
            /* Echo so the user sees what they type. */
            vga_put_char((uint8_t)c);
            if (c == '\n') break;
        }
        __asm__ __volatile__("cli");
        return got;
    }

    default:
        return (uint64_t)-1;
    }
}
