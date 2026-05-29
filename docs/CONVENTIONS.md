# Noxis OS — Code Conventions

This file is the **single source of truth** for all code conventions in this project.
Every line of code must conform. No exceptions, no personal style deviations.

---

## 1. C Code Conventions

### 1.1 Naming

| Category          | Convention              | Example                          |
|-------------------|-------------------------|----------------------------------|
| Types (structs, enums, typedefs) | `snake_case_t` | `page_directory_t`, `process_t`, `os_status_t` |
| Functions (public) | `subsystem_verb_noun` | `pmm_alloc_frame()`, `vga_put_char()` |
| Functions (private/file-scope) | `_underscore_prefix` | `_find_free_block()`, `_merge_adjacent()` |
| Constants / macros | `SCREAMING_SNAKE_CASE` | `PAGE_SIZE`, `KERNEL_HEAP_START`, `IS_PAGE_PRESENT(x)` |
| Global variables | `g_prefix` | `g_kernel_heap`, `g_current_process`, `g_tick_count` |
| Enum values | `OS_OK`, `OS_ERR_NULL` | Subsystem prefix + value |
| Boolean macros | `IS_` / `HAS_` prefix | `IS_PAGE_PRESENT(x)`, `HAS_FLAG(x, f)`, `IS_VALID_PTR(p)` |

### 1.2 Types

- **NEVER use**: `int`, `char`, `short`, `long`, `long long`, `float`, `double`
- **ALWAYS use**: `uint8_t`, `uint16_t`, `uint32_t` (from `<stdint.h>` or our own definitions)
- **Booleans**: `typedef uint8_t bool_t` with `#define TRUE 1` and `#define FALSE 0` — never `<stdbool.h>`
- **Sizes**: Always `uint32_t` — never `size_t` (no stdlib)
- **Pointers to hardware registers**: Always `volatile`
- **NULL**: Use `((void*)0)` — defined in our types.h

```c
/* Correct */
uint32_t count;
volatile uint16_t* vga_buffer;
bool_t is_ready;

/* WRONG */
int count;
size_t size;
bool is_ready;
```

### 1.3 Structs

- Always `typedef struct { ... } name_t;`
- Always `__attribute__((packed))` when mapping to hardware structures
- Fields ordered from largest to smallest to minimize padding (except hardware structs where order is dictated by hardware)
- Never use bit-fields — use bit masks instead for portability with packed structs

```c
typedef struct __attribute__((packed)) {
    uint32_t base_low;      /* Bits 0-15 of base, bits 0-7 of flags */
    uint32_t base_high;     /* Bits 24-31 of base */
    /* ... */
} gdt_entry_t;
```

### 1.4 Functions

- **Maximum 50 lines** per function body. Split if longer.
- Every function returns `os_status_t` UNLESS it returns a pointer (NULL on error) or a count (0 on error).
- Every pointer parameter is checked for NULL at function entry.
- `static` on everything that does not cross compilation units.
- Every public function has a full Doxygen block.
- Avoid more than 5 parameters — use a struct if needed.

```c
/**
 * @brief Allocates a contiguous range of physical frames
 * @param count  Number of frames to allocate (min 1, max 256)
 * @param out    Output: physical base address of the allocated range
 * @return OS_OK on success, OS_ERR_OOM if insufficient free frames,
 *         OS_ERR_NULL if out is NULL, OS_ERR_INVALID if count is 0 or > 256
 */
os_status_t pmm_alloc_frames(uint32_t count, uint32_t* out);
```

### 1.5 Error Handling

**Global status enum — used everywhere:**
```c
typedef enum {
    OS_OK            = 0,  /* Operation succeeded */
    OS_ERR_NULL      = 1,  /* NULL pointer argument */
    OS_ERR_OOM       = 2,  /* Out of memory */
    OS_ERR_INVALID   = 3,  /* Invalid argument */
    OS_ERR_IO        = 4,  /* I/O error */
    OS_ERR_NOT_FOUND = 5,  /* Resource not found */
    OS_ERR_PERM      = 6,  /* Permission denied */
    OS_ERR_BUSY      = 7,  /* Resource busy */
    OS_ERR_RANGE     = 8,  /* Value out of valid range */
    OS_ERR_NOSYS     = 9,  /* Function not implemented */
} os_status_t;
```

**Error handling pattern:**
```c
os_status_t func(uint32_t* out) {
    if (!out) return OS_ERR_NULL;

    os_status_t status = _internal_call();
    if (status != OS_OK) return status;

    *out = value;
    return OS_OK;
}
```

### 1.6 Headers

```c
#ifndef KERNEL_SUBSYSTEM_MODULE_H
#define KERNEL_SUBSYSTEM_MODULE_H

/* ── includes ──────────────────────────────────────────────── */
#include <common/types.h>

/* ── constants ─────────────────────────────────────────────── */
#define MODULE_CONSTANT  0x1234

/* ── types ─────────────────────────────────────────────────── */
typedef struct { ... } module_type_t;

/* ── public functions ──────────────────────────────────────── */
os_status_t module_init(void);
uint32_t   module_get_value(void);

#endif /* KERNEL_SUBSYSTEM_MODULE_H */
```

**Invariant:** One header file per subsystem. No private types or functions in headers.

### 1.7 File Header

**Every single source file starts with:**
```c
/**
 * @file    subsystem/module.c
 * @brief   One-line description of this file's single responsibility
 * @author  YourName
 * @date    YYYY-MM-DD
 */
```

### 1.8 Includes

- Order: standard headers first, then our common headers, then subsystem headers
- No circular includes ever
- Use `#include <common/types.h>` for the `types/` directory
- Guard every header with `#ifndef`

---

## 2. Assembly Conventions (NASM)

### 2.1 Structure

- Explicit sections always: `section .text`, `section .data`, `section .bss`
- One `section .text` per file — multiple if the file has both code and data
- Each ASM file starts with a block comment: filename, purpose, register conventions

### 2.2 Labels

- Public labels: `subsystem_function_name` (matches C naming)
- Local labels: `.local_name` (NASM dot-prefix, scoped to previous non-local label)
- Every label has a comment on the same line or directly above

```nasm
; Switch to the next process. Saves current context, restores next.
context_switch:
    push ebp                ; save caller's frame pointer
    mov  ebp, esp           ; new frame
```

### 2.3 Constants

- **Always `%define`**, never hardcoded magic numbers
- Group related constants with a section comment

```nasm
; ── Page flags ───────────────────────────────────────────────
%define PAGE_PRESENT   0x1
%define PAGE_RW        0x2
%define PAGE_USER      0x4
```

### 2.4 Register Discipline

- Document at function entry which registers are used and for what
- Preserve every register you modify that the calling convention requires preserved
- Use `pusha`/`popa` **only** in interrupt stubs — manual push/pop everywhere else
- The C calling convention (cdecl) preserves: EBX, ESI, EDI, EBP, ESP
- The C calling convention may destroy: EAX, ECX, EDX, EFLAGS

### 2.5 ISR Stub Pattern

**Every ISR stub follows this exact pattern — never deviate:**
```nasm
; CPU has already pushed: SS, ESP, EFLAGS, CS, EIP (and error code for some)
isr_stub_N:
    push dword 0            ; dummy error code (for exceptions without one)
    push dword N            ; interrupt vector number
    jmp  isr_common         ; common handler saves remaining context, calls C
```

### 2.6 Comments

- Every non-obvious instruction gets an inline comment
- Magic hardware values get a named constant + a comment referencing the hardware spec
- Comments explain **why**, not **what** (the instruction already says what)

### 2.7 Calling C from ASM (cdecl)

```nasm
    push dword arg2         ; push arguments right-to-left
    push dword arg1
    call c_function
    add  esp, 8             ; caller cleans the stack
    ; return value is in EAX
```

---

## 3. Directory Structure

```
src/
├── boot/           Bootloader (NASM only)
├── hal/            Hardware Abstraction Layer
│   ├── gdt.c/h     GDT management
│   ├── idt.c/h     IDT management
│   ├── pic.c/h     PIC (8259A) driver
│   ├── pit.c/h     PIT (8253/8254) driver
│   └── ports.c/h   Port I/O wrappers
├── mm/             Memory Management
│   ├── pmm.c/h     Physical Memory Manager
│   ├── vmm.c/h     Virtual Memory Manager + paging ops
│   └── heap.c/h    Kernel heap allocator
├── drivers/        Device drivers
│   ├── vga.c/h     VGA text mode
│   ├── keyboard.c/h PS/2 keyboard
│   └── ata.c/h     ATA PIO driver
├── kernel/         Core kernel
│   ├── early.c     Early init (zero BSS, init HAL)
│   ├── panic.c/h   Kernel panic
│   └── isr.c/h     ISR dispatcher
├── asm/            Pure assembly files
│   ├── isr_stubs.asm   ISR entry stubs
│   ├── switch.asm      Context switch
│   └── boot.asm        Load GDT/IDT (lgdt, lidt)
├── vfs/            Virtual File System
├── proc/           Process management + scheduler
├── syscall/        System call interface
└── common/         Shared types and constants
    ├── types.h     uintX_t, bool_t, NULL
    └── status.h    os_status_t enum
```

---

## 4. Build Conventions

- Cross-compiler: `i686-elf-gcc`
- C flags: `-std=c11 -ffreestanding -nostdlib -nostdinc -Wall -Wextra -Werror`
- C flags (debug): add `-g -O0`
- C flags (release): add `-O2`
- NASM flags: `-f elf32`
- Linker: custom linker script, `i686-elf-ld`
- Output: ELF32 executable, linked at virtual address `0xC0100000`
- Physical load address: `0x00100000`
- QEMU: `qemu-system-i386 -cdrom noxis.iso` or `-kernel noxis.elf`
- GDB: `i686-elf-gdb` with `.gdbinit` for QEMU remote debugging

---

## 5. Version Control Conventions

See `docs/COMMIT_CONVENTIONS.md` for full details.

---

## 6. Hard Rules (Zero Tolerance)

1. No `#include` of standard library headers (`stdio.h`, `stdlib.h`, `string.h`, etc.)
2. No `size_t`, `ssize_t`, `ptrdiff_t`, `intptr_t`, `wchar_t`
3. No `bool`, `true`, `false` from `<stdbool.h>`
4. No `NULL` from `<stddef.h>` — define our own
5. No inline assembly in `.c` files — all ASM goes in `.asm` files
6. No dynamic memory allocation before PMM and heap are initialized
7. No floating-point operations in kernel code
8. No recursive functions (kernel stack is limited)
9. Max 50 lines per function
10. Every file starts with the file header doc comment
