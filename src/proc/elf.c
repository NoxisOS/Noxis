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

#define PT_LOAD       1
#define PHYSMAP_BASE  0xFFFF800000000000ULL

/* Translate a user VA already mapped in pml4_phys to its backing frame, so we
   can write the segment image through the physmap without switching CR3. */
static uint64_t va_to_phys(uint64_t pml4_phys, uint64_t va) {
    uint64_t i4 = (va >> 39) & 0x1FF, i3 = (va >> 30) & 0x1FF;
    uint64_t i2 = (va >> 21) & 0x1FF, i1 = (va >> 12) & 0x1FF;
    uint64_t* pml4 = (uint64_t*)(PHYSMAP_BASE + pml4_phys);
    if (!(pml4[i4] & 1)) return 0;
    uint64_t* pdpt = (uint64_t*)(PHYSMAP_BASE + (pml4[i4] & ~0xFFFULL));
    if (!(pdpt[i3] & 1)) return 0;
    uint64_t* pd = (uint64_t*)(PHYSMAP_BASE + (pdpt[i3] & ~0xFFFULL));
    if (!(pd[i2] & 1)) return 0;
    uint64_t* pt = (uint64_t*)(PHYSMAP_BASE + (pd[i2] & ~0xFFFULL));
    if (!(pt[i1] & 1)) return 0;
    return (pt[i1] & ~0xFFFULL) | (va & 0xFFF);
}

/* Load an ELF64 image into the address space pml4_phys. Returns entry, or 0. */
uint64_t elf64_load_into(uint64_t pml4_phys, const uint8_t* img) {
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

        uint64_t va     = ph[i].p_vaddr;
        uint64_t vstart = va & ~0xFFFULL;
        uint64_t vend   = (va + ph[i].p_memsz + 0xFFF) & ~0xFFFULL;

        for (uint64_t p = vstart; p < vend; p += 0x1000) {
            uint64_t fr = pmm_alloc_frame();
            if (!fr) return 0;
            vmm_map_page_into(pml4_phys, p, fr, PAGE_RW | PAGE_USER);
        }

        /* Copy file data + zero BSS through the physmap, byte by byte
           (segments may straddle page boundaries with non-contiguous frames). */
        const uint8_t* src = img + ph[i].p_offset;
        for (uint64_t b = 0; b < ph[i].p_memsz; b++) {
            uint64_t phys = va_to_phys(pml4_phys, va + b);
            if (!phys) return 0;
            *(uint8_t*)(PHYSMAP_BASE + phys) = (b < ph[i].p_filesz) ? src[b] : 0;
        }
    }

    return eh->e_entry;
}

/* Backward-compatible wrapper: load into the current (kernel) address space. */
uint64_t elf64_load(const uint8_t* img) {
    return elf64_load_into(vmm_kernel_pml4(), img);
}
