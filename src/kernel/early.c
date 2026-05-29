/**
 * @file    kernel/early.c
 * @brief   Early kernel initialization — user mode demo
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
#include <proc/process.h>
#include <proc/scheduler.h>
#include <proc/user.h>
#include <syscall/syscall.h>

#define VGA_WIDTH    80
#define VGA_HEIGHT   25
#define VGA_BUFFER   ((volatile uint16_t*)0xB8000)
#define VGA_COLOR(fg, bg)  ((uint8_t)(((bg) << 4) | ((fg) & 0x0F)))
#define VGA_BLACK         0x0
#define VGA_LIGHT_GREY    0x7
#define VGA_GREEN         0x2

static uint32_t g_row, g_col;
static uint8_t  g_color;

static void _vga_clear(void) {
    uint16_t b = (uint16_t)' ' | ((uint16_t)g_color << 8);
    for (uint32_t i = 0; i < VGA_WIDTH*VGA_HEIGHT; i++) VGA_BUFFER[i]=b;
    g_row=g_col=0;
}
static void _vga_scroll(void) {
    for (uint32_t i = 0; i < VGA_WIDTH*(VGA_HEIGHT-1); i++)
        VGA_BUFFER[i]=VGA_BUFFER[i+VGA_WIDTH];
    uint16_t b = (uint16_t)' ' | ((uint16_t)g_color << 8);
    for (uint32_t i = 0; i < VGA_WIDTH; i++)
        VGA_BUFFER[VGA_WIDTH*(VGA_HEIGHT-1)+i]=b;
}
static void _vga_put_char(uint8_t c) {
    if (c=='\n'){g_col=0;g_row++;}
    else if(c=='\r'){g_col=0;}
    else{VGA_BUFFER[g_row*VGA_WIDTH+g_col]=(uint16_t)c|((uint16_t)g_color<<8);g_col++;if(g_col>=VGA_WIDTH){g_col=0;g_row++;}}
    if(g_row>=VGA_HEIGHT){_vga_scroll();g_row=VGA_HEIGHT-1;}
}
static void _vga_write(const uint8_t* s) {
    for(uint32_t i=0;s[i];i++)_vga_put_char(s[i]);
}

extern void _user_demo_start(void);
extern void _user_demo_end(void);

void kernel_main(void) {
    g_color = VGA_COLOR(VGA_LIGHT_GREY, VGA_BLACK);
    _vga_clear();

    _vga_write((const uint8_t*)"\n   Noxis OS v0.5.0\n   ===============\n\n");
    _vga_write((const uint8_t*)"   [HAL] GDT... "); gdt_init(); _vga_write((const uint8_t*)"OK\n");
    _vga_write((const uint8_t*)"   [HAL] IDT... "); idt_init(); _vga_write((const uint8_t*)"OK\n");
    _vga_write((const uint8_t*)"   [HAL] PIC... "); pic_remap(); _vga_write((const uint8_t*)"OK\n");
    _vga_write((const uint8_t*)"   [KRN] ISR... "); isr_init(); _vga_write((const uint8_t*)"OK\n");
    _vga_write((const uint8_t*)"   [MM]  PMM... "); pmm_init(128*1024*1024); _vga_write((const uint8_t*)"OK\n");
    _vga_write((const uint8_t*)"   [MM]  VMM... "); _vga_write((const uint8_t*)"OK\n");
    _vga_write((const uint8_t*)"   [MM]  HEAP.. "); heap_init(); _vga_write((const uint8_t*)"OK\n");
    _vga_write((const uint8_t*)"   [DRV] PIT... "); pit_init(1000); _vga_write((const uint8_t*)"OK\n");
    _vga_write((const uint8_t*)"   [PROC] SCHED.. "); scheduler_init(); _vga_write((const uint8_t*)"OK\n");
    _vga_write((const uint8_t*)"   [SYS] SYSCALL. "); syscall_init(); _vga_write((const uint8_t*)"OK\n");

    cpu_sti();

    /* Copy user demo code to user-accessible page */
    uint32_t user_code_phys, user_stack_phys;
    pmm_alloc_frame(&user_code_phys);
    pmm_alloc_frame(&user_stack_phys);

    /* Map user code at 0x400000 (ring 3 readable, kernel RW) */
    vmm_map_page(0x400000, user_code_phys, PAGE_PRESENT | PAGE_RW | PAGE_USER);
    vmm_map_page(0x500000, user_stack_phys, PAGE_PRESENT | PAGE_RW | PAGE_USER);

    /* Copy user code to the page */
    uint32_t code_size = (uint32_t)&_user_demo_end - (uint32_t)&_user_demo_start;
    for (uint32_t i = 0; i < code_size; i++) {
        ((volatile uint8_t*)0x400000)[i] = ((uint8_t*)&_user_demo_start)[i];
    }

    _vga_write((const uint8_t*)"\n   Entering user mode...\n");

    /* Jump to ring 3 (sets TSS ESP0 for return path) */
    gdt_set_kernel_stack(0xD0000000 + 0x10000); /* temp kernel stack for user→kernel */
    user_enter(0x400000, 0x500000 + 0x1000);    /* entry, stack top */
}
