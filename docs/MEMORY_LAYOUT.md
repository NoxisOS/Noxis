# Noxis OS — Memory Layout

## 1. Physical Memory Map

```
Address Range             Size        Purpose
─────────────────────────────────────────────────────────────────
0x00000000 - 0x000003FF    1 KB      Real mode IVT (not used after boot)
0x00000400 - 0x000004FF    256 B     BIOS Data Area (BDA)
0x00000500 - 0x00007BFF   ~30 KB     Bootloader stack / scratch
0x00007C00 - 0x00007DFF    512 B     MBR (loaded by BIOS)
0x00007E00 - 0x0007FFFF   ~480 KB    Stage2 / free low memory
0x00080000 - 0x0009FFFF    128 KB    EBDA (Extended BIOS Data Area) — AVOID
0x000A0000 - 0x000BFFFF    128 KB    Video memory + VGA BIOS
0x000C0000 - 0x000FFFFF    256 KB    Option ROMs + BIOS ROM
0x00100000 - 0x001FFFFF    1 MB      Kernel image (code + data + BSS)
0x00200000 - 0x002FFFFF    1 MB      PMM bitmap (tracks 4 GB of physical memory)
0x00300000 - 0x003FFFFF    1 MB      Kernel stack area (multiple stacks)
0x00400000 - END_OF_RAM    variable   Free physical frames (managed by PMM)
```

**Constants:**
```c
#define KERNEL_PHYS_BASE       0x00100000
#define KERNEL_MAX_SIZE        0x00100000  /* 1 MB reserved for kernel */
#define PMM_BITMAP_START       0x00200000
#define PMM_BITMAP_SIZE        0x00080000  /* 512 KB → 4 GB tracked */
#define KERNEL_STACK_START     0x00300000
#define KERNEL_STACK_SIZE      0x00100000  /* 1 MB stack area, 16 × 64 KB stacks */
#define FREE_MEMORY_START      0x00400000
```

**PMM Bitmap Math:**
- 4 GB physical address space / 4 KB frames = 1,048,576 frames
- 1,048,576 bits / 8 = 131,072 bytes (128 KB)
- We allocate 512 KB for safety margin and future growth

---

## 2. Virtual Memory Map (After Paging Enabled)

```
Virtual Address Range              Size        Purpose
─────────────────────────────────────────────────────────────────
0x00000000 - 0xBFFFFFFF            3 GB        User space
  0x00000000 - 0x00000FFF           4 KB        Guard page (unmapped, catches NULL ptr)
  0x00001000 - 0x000FFFFF          ~1 MB        User code/text
  0x00100000 - 0x001FFFFF           1 MB        User data
  0x00200000 - 0xBFFFFFFF         ~2.998 GB     User heap (grows upward)
                                                User stack (grows downward from
                                                thread-specific top)

0xC0000000 - 0xFFFFFFFF            1 GB        Kernel space
  0xC0000000 - 0xC00FFFFF           1 MB        Kernel code (mapped to phys 0x100000)
  0xC0100000 - 0xC01FFFFF           1 MB        Kernel rodata + data
  0xC0200000 - 0xC02FFFFF           1 MB        Kernel BSS
  0xC0300000 - 0xC03FFFFF           1 MB        Reserved
  0xC0400000 - 0xC07FFFFF           4 MB        Kernel heap (kmalloc)
  0xC0800000 - 0xFFBFFFFF        ~1019 MB       Free kernel virtual space
  0xFFC00000 - 0xFFDFFFFF           2 MB        Temporary page mappings
  0xFFE00000 - 0xFFFFEFFF       ~2047 KB        Page directory + page table pool
  0xFFFFF000 - 0xFFFFF007           8 B         Recursive PD entry (PD[1023] → PD itself)
  0xFFFFF008 - 0xFFFFFFFF      ~4092 B - 8 B   Unused (no 4 MB page at top)
```

**Details:**

### Recursive Paging
The last PDE in the page directory (index 1023) points back to the physical address of the page directory itself. This makes the page directory and all page tables accessible at well-known virtual addresses:

```c
#define RECURSIVE_PD_INDEX      1023
#define RECURSIVE_PD_VADDR      0xFFFFF000  /* PD[1023] mapped here */

/* To access any PDE: */
#define PDE_VADDR(dir_idx)      (0xFFC00000 + (dir_idx) * 0x1000)

/* To access any PTE: */
#define PTE_VADDR(dir_idx, tbl_idx)  \
    (0xFFC00000 + (dir_idx) * 0x400000 + (tbl_idx) * 0x1000)
```

### Temporary Mappings
When the kernel needs to modify a page that is not currently mapped, it uses the temporary mapping area:
- PD index 1022 is the "temp" slot
- Only one temporary mapping at a time
- Protected by a simple spinlock (mask interrupts during use)

---

## 3. Kernel Stack Layout

Each kernel thread/process gets a 16 KB kernel stack:

```
Offset from top    Contents
─────────────────────────────────────
  0                Top of stack (ESP after switch)
 -4                SS (popped by iret)
 -8                ESP (user ESP, popped by iret)
-12                EFLAGS (popped by iret)
-16                CS (popped by iret)
-20                EIP (popped by iret)
-24                Error code (if applicable)
-28                Interrupt number
-32                GS
-36                FS
-40                ES
-44                DS
-48                EDI
-52                ESI
-56                EBP
-60                EBX
-64                EDX
-68                ECX
-72                EAX
─────────────────────────────────────
 Bottom            Start of stack (0x4000 below top)
```

Note: x86 stacks grow downward. The layout above shows offsets from the top (highest address).

---

## 4. Page Table / Page Directory Entry Format

### Page Directory Entry (PDE)
```
Bit 31 ....................................... Bit 0
[ Page Table Base Address (bits 31-12) ] [Avail] [G] [PS] [0] [A] [PCD] [PWT] [U/S] [R/W] [P]
                                                                                         ^     ^    ^
                                                                                         |     |    Present
                                                                                         |     Read/Write (1=RW)
                                                                                         User/Supervisor (1=user)
```

### Page Table Entry (PTE)
```
Bit 31 ....................................... Bit 0
[ Page Frame Base Address (bits 31-12) ] [Avail] [G] [PAT] [D] [A] [PCD] [PWT] [U/S] [R/W] [P]
                                                                                               ^
                                                                                               Present
```

**Flags:**
```c
#define PAGE_PRESENT       0x001   /* Page is in memory */
#define PAGE_RW            0x002   /* Read-write (0 = read-only) */
#define PAGE_USER          0x004   /* User-accessible (0 = supervisor only) */
#define PAGE_WRITETHROUGH  0x008   /* Write-through caching */
#define PAGE_CACHE_DISABLE 0x010   /* Cache disabled */
#define PAGE_ACCESSED      0x020   /* Accessed (CPU sets this) */
#define PAGE_DIRTY         0x040   /* Dirty (CPU sets this, PTEs only) */
#define PAGE_SIZE_4MB      0x080   /* Page size (0=4KB, 1=4MB, PDEs only) */
#define PAGE_GLOBAL        0x100   /* Global (not flushed on CR3 reload) */
```

---

## 5. PMM Bitmap Design

The Physical Memory Manager uses a bitmap where each bit represents one 4 KB frame:
- Bit = 0: frame is free
- Bit = 1: frame is allocated

The bitmap is located at physical address `PMM_BITMAP_START` (0x200000).
It covers all frames from 0x00000000 to 0xFFFFFFFF (4 GB).

The following regions are pre-marked as "used" at boot:
- 0x00000000 – 0x000FFFFF (BIOS, IVT, BDA, EBDA, ROMs)
- 0x00100000 – 0x001FFFFF (kernel image)
- 0x00200000 – 0x0027FFFF (PMM bitmap itself)
- 0x00300000 – 0x003FFFFF (kernel stacks)

All other usable RAM is marked free based on the BIOS/multiboot memory map.

