/**
 * @file    kernel/early.c
 * @brief   Early kernel initialization
 * @author  Noxis Team
 * @date    2026-05-30
 */
#include <common/types.h>
#include <hal/gdt.h>
#include <hal/idt.h>
#include <hal/pic.h>
#include <hal/ports.h>
#include <kernel/isr.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <mm/heap.h>
#include <mm/paging.h>
#include <drivers/pit.h>
#include <drivers/ata.h>
#include <proc/process.h>
#include <proc/scheduler.h>
#include <syscall/syscall.h>

#define VGA_BUFFER   ((volatile uint16_t*)0xB8000)
#define VGA_WIDTH    80

#define BLACK         0x0
#define DARK_GREY     0x8
#define LIGHT_GREY    0x7
#define WHITE         0xF
#define GREEN         0x2
#define CYAN          0x3

static uint8_t  g_color;
static uint32_t g_row, g_col;

static void _vga_put(uint8_t c) {
    if (c == '\n') { g_col = 0; g_row++; return; }
    VGA_BUFFER[g_row * VGA_WIDTH + g_col] = (uint16_t)c | ((uint16_t)g_color << 8);
    if (++g_col >= VGA_WIDTH) { g_col = 0; g_row++; }
}
static void _vw(const uint8_t* s) { for (; *s; s++) _vga_put(*s); }
static void _vcolor(uint8_t c) { g_color = c; }
static void _vcenter(const uint8_t* s) {
    uint32_t len = 0; while (s[len]) len++;
    g_col = (VGA_WIDTH - len) / 2;
    for (; *s; s++) _vga_put(*s);
}

static os_status_t _step_pmm(void)   { return pmm_init(128*1024*1024); }
static os_status_t _step_heap(void)  { return heap_init(); }
static os_status_t _step_pit(void)   { return pit_init(1000); }
static os_status_t _step_ata(void)   { return ata_init(ATA_PRIMARY, ATA_MASTER); }
static os_status_t _step_sched(void) { return scheduler_init(); }
static os_status_t _step_isr(void)   { isr_init(); return OS_OK; }
static os_status_t _step_sys(void)   { return syscall_init(); }

static os_status_t _step(const uint8_t* name, os_status_t (*fn)(void)) {
    _vcolor(WHITE); _vw((const uint8_t*)" ["); _vcolor(GREEN); _vw(name);
    _vcolor(DARK_GREY); _vw((const uint8_t*)"..");
    if (fn() == OS_OK) { _vcolor(GREEN); _vw((const uint8_t*)"OK"); }
    else               { _vcolor(0x4);   _vw((const uint8_t*)"FAIL"); }
    _vcolor(WHITE); _vw((const uint8_t*)"]\n");
    return OS_OK;
}

void kernel_main(void) {
    g_color = LIGHT_GREY;
    g_row = g_col = 0;
    for (uint32_t i=0;i<VGA_WIDTH*25;i++) VGA_BUFFER[i]=0x0F20;

    _vcolor(BLACK);
    _vw((const uint8_t*)"\n\n\n\n");
    _vcolor(CYAN);
    _vcenter((const uint8_t*)"  _   _            _         ___  ___ ");
    _vw((const uint8_t*)"\n");
    _vcenter((const uint8_t*)" | \\ | | _____  __(_)___    |  _||_  |");
    _vw((const uint8_t*)"\n");
    _vcenter((const uint8_t*)" |  \\| |/ _ \\ \\/ /| / __|   | |_   | |");
    _vw((const uint8_t*)"\n");
    _vcenter((const uint8_t*)" | |\\  | (_) >  < | \\__ \\   |  _|  | |");
    _vw((const uint8_t*)"\n");
    _vcenter((const uint8_t*)" |_| \\_|\\___/_/\\_\\|_|___/   |_|   |_|");
    _vw((const uint8_t*)"\n\n");
    _vcolor(WHITE);
    _vcenter((const uint8_t*)"32-bit Operating System");
    _vw((const uint8_t*)"\n");
    _vcenter((const uint8_t*)"Built from scratch in C11 & x86 Assembly");
    _vw((const uint8_t*)"\n\n\n");

    _vcolor(DARK_GREY);
    g_col = 23;
    _step((const uint8_t*)"GDT   ", gdt_init);
    g_col = 23;
    _step((const uint8_t*)"IDT   ", idt_init);
    g_col = 23;
    _step((const uint8_t*)"PIC   ", pic_remap);
    g_col = 23;
    _step((const uint8_t*)"ISR   ", _step_isr);
    g_col = 23;
    _step((const uint8_t*)"PMM   ", _step_pmm);
    g_col = 23;
    _vcolor(WHITE); _vw((const uint8_t*)" [VMM.. "); _vcolor(GREEN); _vw((const uint8_t*)"OK"); _vcolor(WHITE); _vw((const uint8_t*)"]\n");
    g_col = 23;
    _step((const uint8_t*)"HEAP  ", _step_heap);
    g_col = 23;
    _step((const uint8_t*)"PIT   ", _step_pit);
    g_col = 23;
    _step((const uint8_t*)"ATA   ", _step_ata);
    g_col = 23;
    _step((const uint8_t*)"SCHED ", _step_sched);
    g_col = 23;
    _step((const uint8_t*)"SYSCALL", _step_sys);

    _vw((const uint8_t*)"\n");
    g_col = 20;
    _vcolor(CYAN); _vw((const uint8_t*)"Kernel: 0xC0100000 | 32-bit Protected Mode | Paging Active");
    _vw((const uint8_t*)"\n");
    g_col = 20;
    _vcolor(LIGHT_GREY); _vw((const uint8_t*)"All subsystems initialized. System halted.\n");
    for (;;);
}
