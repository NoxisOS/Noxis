/**
 * @file    proc/elf.h
 * @brief   ELF32 loader — maps PT_LOAD segments and returns entry point.
 * @author  Noxis Team
 * @date    2026-05-30
 */
#ifndef PROC_ELF_H
#define PROC_ELF_H

#include <common/types.h>
#include <common/status.h>

/**
 * @brief Parse an ELF32 binary in `elf` and load all PT_LOAD segments.
 *        Allocates physical frames, maps user-accessible pages, copies
 *        the file bytes, zeroes BSS.
 * @param elf       pointer to the ELF file in memory
 * @param size      file size in bytes
 * @param entry_out output: virtual entry address (e_entry)
 * @return OS_OK on success
 */
os_status_t elf_load(const uint8_t* elf, uint32_t size, uint32_t* entry_out);

#endif /* PROC_ELF_H */
