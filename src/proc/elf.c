/**
 * @file    proc/elf.c
 * @brief   ELF32 loader — minimal: only PT_LOAD, no relocations,
 *          no dynamic linking, no security checks. Maps every loaded
 *          page as user RW so the kernel can copy bytes into them.
 * @author  Noxis Team
 * @date    2026-05-30
 */
#include <proc/elf.h>
#include <mm/phys/pmm.h>
#include <mm/virt/vmm.h>
#include <mm/virt/paging.h>
#include <common/types.h>

/* ── ELF32 structures (subset) ──────────────────────────────── */

#define EI_MAG0  0
#define EI_MAG1  1
#define EI_MAG2  2
#define EI_MAG3  3
#define ELFMAG0  0x7F
#define ELFMAG1  'E'
#define ELFMAG2  'L'
#define ELFMAG3  'F'

#define ET_EXEC  2
#define EM_386   3
#define PT_LOAD  1

typedef struct __attribute__((packed)) {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} elf32_ehdr_t;

typedef struct __attribute__((packed)) {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} elf32_phdr_t;

/* ── public ─────────────────────────────────────────────────── */

os_status_t elf_load(const uint8_t* elf, uint32_t size, uint32_t* entry_out) {
    if (!elf || !entry_out) return OS_ERR_NULL;
    if (size < sizeof(elf32_ehdr_t)) return OS_ERR_INVALID;

    const elf32_ehdr_t* eh = (const elf32_ehdr_t*)elf;
    if (eh->e_ident[EI_MAG0] != ELFMAG0 ||
        eh->e_ident[EI_MAG1] != ELFMAG1 ||
        eh->e_ident[EI_MAG2] != ELFMAG2 ||
        eh->e_ident[EI_MAG3] != ELFMAG3) return OS_ERR_INVALID;
    if (eh->e_machine != EM_386) return OS_ERR_INVALID;
    if (eh->e_phoff + eh->e_phnum * sizeof(elf32_phdr_t) > size) return OS_ERR_INVALID;

    const elf32_phdr_t* phdrs = (const elf32_phdr_t*)(elf + eh->e_phoff);

    for (uint32_t i = 0; i < eh->e_phnum; i++) {
        const elf32_phdr_t* ph = &phdrs[i];
        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_memsz < ph->p_filesz) return OS_ERR_INVALID;
        if (ph->p_offset + ph->p_filesz > size) return OS_ERR_INVALID;

        uint32_t vstart = PAGE_ALIGN_DOWN(ph->p_vaddr);
        uint32_t vend   = PAGE_ALIGN_UP(ph->p_vaddr + ph->p_memsz);

        /* Map every page in the segment, zero-fill, then copy file data. */
        for (uint32_t v = vstart; v < vend; v += PAGE_SIZE) {
            uint32_t phys;
            if (pmm_alloc_frame(&phys) != OS_OK) return OS_ERR_OOM;
            if (vmm_map_page(v, phys,
                             PAGE_PRESENT | PAGE_RW | PAGE_USER) != OS_OK) {
                return OS_ERR_IO;
            }
            volatile uint8_t* p = (volatile uint8_t*)v;
            for (uint32_t j = 0; j < PAGE_SIZE; j++) p[j] = 0;
        }

        const uint8_t*    src = elf + ph->p_offset;
        volatile uint8_t* dst = (volatile uint8_t*)ph->p_vaddr;
        for (uint32_t j = 0; j < ph->p_filesz; j++) dst[j] = src[j];
    }

    *entry_out = eh->e_entry;
    return OS_OK;
}
