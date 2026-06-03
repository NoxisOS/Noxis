/**
 * @file    kernel/core/early.c
 * @brief   64-bit kernel entry — brings up the core subsystems.
 *
 * NOTE: This is the x86_64 port in progress. The full init sequence
 * (scheduler, syscalls, VFS, userland…) is being ported phase by phase;
 * for now kernel_main brings up GDT/IDT/PMM/VMM/heap and halts.
 */
#include <common/types.h>
#include <drivers/serial.h>
#include <drivers/pit.h>
#include <drivers/vga.h>
#include <drivers/kbd.h>
#include <drivers/keymap.h>
#include <drivers/ata.h>
#include <fs/vfs/vfs.h>
#include <kernel/hal/gdt.h>
#include <kernel/hal/pic.h>
#include <kernel/hal/fpu.h>
#include <kernel/hal/ports.h>
#include <kernel/isr/isr.h>
#include <mm/phys/pmm.h>
#include <mm/virt/vmm.h>
#include <mm/virt/heap.h>
#include <proc/scheduler.h>

void serial_write_hex64(uint64_t v);

int32_t kbd_poll(void);
os_status_t kbd_init(void);
os_status_t syscall_init(void);
extern void enter_ring3(uint64_t entry, uint64_t user_rsp);
uint64_t elf64_load(const uint8_t* img);
extern uint8_t hello_elf_start[], hello_elf_end[];

#define USTACK_VA  0x50000000ULL   /* user stack page (outside identity map) */

void kernel_main(void) {
    vga_init();
    vga_clear();

    serial_init();
    serial_write((const uint8_t*)"\n[noxis64] kernel_main reached\n");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_write((const uint8_t*)"Noxis OS  --  x86_64\n");

    serial_write((const uint8_t*)"[noxis64] GDT ... ");
    gdt_init();
    serial_write((const uint8_t*)"OK\n");

    serial_write((const uint8_t*)"[noxis64] IDT ... ");
    isr_init();
    serial_write((const uint8_t*)"OK\n");

    serial_write((const uint8_t*)"[noxis64] PIC ... ");
    pic_remap();
    serial_write((const uint8_t*)"OK\n");

    serial_write((const uint8_t*)"[noxis64] FPU ... ");
    fpu_init();
    serial_write((const uint8_t*)"OK\n");

    pmm_init(128ULL * 1024 * 1024);
    vmm_init();
    heap_init();

    /* Smoke test: alloc + map + heap. */
    uint64_t fr = pmm_alloc_frame();
    vmm_map_page(0x40000000ULL, fr, PAGE_RW);
    volatile uint64_t* t = (volatile uint64_t*)0x40000000ULL;
    *t = 0xCAFEBABEDEADBEEFULL;
    serial_write((const uint8_t*)"[noxis64] paging test ");
    serial_write((const uint8_t*)(*t == 0xCAFEBABEDEADBEEFULL ? "PASS\n" : "FAIL\n"));

    void* a = kmalloc(64);
    kfree(a);
    serial_write((const uint8_t*)"[noxis64] heap test ");
    serial_write((const uint8_t*)(a ? "PASS\n" : "FAIL\n"));

    /* ── ATA disk: read sector 0 (boot sector), check 0xAA55 ──── */
    serial_write((const uint8_t*)"[noxis64] ATA ... ");
    ata_init(ATA_PRIMARY, ATA_MASTER);
    {
        static uint16_t sec[256];
        if (ata_read(ATA_PRIMARY, ATA_MASTER, 0, 1, sec) == OS_OK) {
            uint8_t* b = (uint8_t*)sec;
            serial_write((const uint8_t*)(b[510] == 0x55 && b[511] == 0xAA
                ? "OK (read sector 0, 0xAA55 found)\n"
                : "read ok but no boot signature\n"));
        } else {
            serial_write((const uint8_t*)"FAIL (ata_read)\n");
        }
    }

    /* ── VFS (NoxFS on disk, falls back to ramfs) ─────────────── */
    serial_write((const uint8_t*)"[noxis64] VFS ... ");
    vfs_init();
    serial_write((const uint8_t*)"OK (files=");
    serial_write_hex64(vfs_count());
    serial_write((const uint8_t*)")\n");

    /* ── Timer + keyboard + interrupts ────────────────────────── */
    serial_write((const uint8_t*)"[noxis64] PIT ... ");
    pit_init(1000);
    serial_write((const uint8_t*)"OK\n");

    serial_write((const uint8_t*)"[noxis64] KEYMAP ... ");
    keymap_init();
    serial_write((const uint8_t*)"OK\n");

    serial_write((const uint8_t*)"[noxis64] KBD ... ");
    kbd_init();
    serial_write((const uint8_t*)"OK\n");

    /* ── Syscalls + ring 3 test ───────────────────────────────── */
    serial_write((const uint8_t*)"[noxis64] SYSCALL ... ");
    syscall_init();
    serial_write((const uint8_t*)"OK\n");

    /* Load the embedded ELF64 user program. */
    serial_write((const uint8_t*)"[noxis64] ELF64 load ... ");
    uint64_t entry = elf64_load(hello_elf_start);
    serial_write((const uint8_t*)"entry="); serial_write_hex64(entry);
    serial_write((const uint8_t*)"\n");

    /* User stack page. */
    vmm_map_page(USTACK_VA, pmm_alloc_frame(), PAGE_RW | PAGE_USER);

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_write((const uint8_t*)"exec ELF64 ring-3 program...\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    serial_write((const uint8_t*)"[noxis64] entering ring 3\n");

    cpu_sti();
    enter_ring3(entry, USTACK_VA + 0x1000);

    for (;;) __asm__ __volatile__("hlt");        /* unreachable */
}
