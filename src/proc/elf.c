/**
 * @file    proc/elf.c
 * @brief   ELF64 loader — minimal: PT_LOAD segments only, no relocations.
 *
 * Maps each loadable segment (USER|RW) into the current address space,
 * copies file data, zero-fills BSS, and returns the entry point.
 */
#include <common/types.h>
#include <mm/phys/pmm.h>
#include <mm/virt/vmm.h>
#include <drivers/serial.h>

void serial_write_hex64(uint64_t v);

typedef struct __attribute__((packed)) {
    uint8_t  e_ident[16];
    uint16_t e_type, e_machine;
    uint32_t e_version;
    uint64_t e_entry, e_phoff, e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} elf64_ehdr;

typedef struct __attribute__((packed)) {
    uint32_t p_type, p_flags;
    uint64_t p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
} elf64_phdr;

#define PT_LOAD  1

/* Load an in-memory ELF64 image. Returns the entry VA, or 0 on error. */
uint64_t elf64_load(const uint8_t* img) {
    const elf64_ehdr* eh = (const elf64_ehdr*)img;
    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L'  || eh->e_ident[3] != 'F') {
        serial_write((const uint8_t*)"[noxis64] elf: bad magic\n");
        return 0;
    }
    if (eh->e_ident[4] != 2) {   /* ELFCLASS64 */
        serial_write((const uint8_t*)"[noxis64] elf: not 64-bit\n");
        return 0;
    }

    const elf64_phdr* ph = (const elf64_phdr*)(img + eh->e_phoff);
    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD) continue;

        uint64_t va    = ph[i].p_vaddr;
        uint64_t vstart = va & ~0xFFFULL;
        uint64_t vend   = (va + ph[i].p_memsz + 0xFFF) & ~0xFFFULL;

        for (uint64_t p = vstart; p < vend; p += 0x1000) {
            uint64_t fr = pmm_alloc_frame();
            if (!fr) return 0;
            vmm_map_page(p, fr, PAGE_RW | PAGE_USER);
        }

        const uint8_t* src = img + ph[i].p_offset;
        uint8_t* dst = (uint8_t*)va;
        for (uint64_t b = 0; b < ph[i].p_filesz; b++) dst[b] = src[b];
        for (uint64_t b = ph[i].p_filesz; b < ph[i].p_memsz; b++) dst[b] = 0;
    }

    return eh->e_entry;
}
