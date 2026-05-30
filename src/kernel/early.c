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
#include <proc/user.h>
#include <syscall/syscall.h>

#define VGA_WIDTH    80
#define VGA_HEIGHT   25
#define VGA_BUFFER   ((volatile uint16_t*)0xB8000)
#define VGA_COLOR(fg, bg)  ((uint8_t)(((bg) << 4) | ((fg) & 0x0F)))
#define VGA_BLACK         0x0
#define VGA_LIGHT_GREY    0x7

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

void kernel_main(void) {
    g_color = VGA_COLOR(VGA_LIGHT_GREY, VGA_BLACK);
    _vga_clear();

    _vga_write((const uint8_t*)"\n   Noxis OS v0.6.0\n   ===============\n\n");
    _vga_write((const uint8_t*)"   [HAL] GDT... "); gdt_init(); _vga_write((const uint8_t*)"OK\n");
    _vga_write((const uint8_t*)"   [HAL] IDT... "); idt_init(); _vga_write((const uint8_t*)"OK\n");
    _vga_write((const uint8_t*)"   [HAL] PIC... "); pic_remap(); _vga_write((const uint8_t*)"OK\n");
    _vga_write((const uint8_t*)"   [KRN] ISR... "); isr_init(); _vga_write((const uint8_t*)"OK\n");
    _vga_write((const uint8_t*)"   [MM]  PMM... "); pmm_init(128*1024*1024); _vga_write((const uint8_t*)"OK\n");
    _vga_write((const uint8_t*)"   [MM]  VMM... "); _vga_write((const uint8_t*)"OK\n");
    _vga_write((const uint8_t*)"   [MM]  HEAP.. "); heap_init(); _vga_write((const uint8_t*)"OK\n");
    _vga_write((const uint8_t*)"   [DRV] PIT... "); pit_init(1000); _vga_write((const uint8_t*)"OK\n");
    _vga_write((const uint8_t*)"   [DRV] ATA... "); ata_init(ATA_PRIMARY, ATA_MASTER); _vga_write((const uint8_t*)"OK\n");
    _vga_write((const uint8_t*)"   [PROC] SCHED.. "); scheduler_init(); _vga_write((const uint8_t*)"OK\n");
    _vga_write((const uint8_t*)"   [SYS] SYSCALL. "); syscall_init(); _vga_write((const uint8_t*)"OK\n");
    _vga_write((const uint8_t*)"\n   System halted.\n");
    for (;;);
}
