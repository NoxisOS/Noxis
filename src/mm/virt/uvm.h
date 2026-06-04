/**
 * @file    mm/virt/uvm.h
 * @brief   User virtual-address-space layout shared by exec and the
 *          page-fault handler.
 *
 *  The user stack grows DOWN from USER_STACK_TOP.  exec_run eagerly maps
 *  only the top page (argv/argc are built there before entering ring 3);
 *  every lower page is mapped on demand by the page-fault handler the
 *  first time the program touches it, up to USER_STACK_PAGES total.
 *
 * @author  Noxis Team
 * @date    2026-05-30
 */
#ifndef MM_VIRT_UVM_H
#define MM_VIRT_UVM_H

/* User stack: one page is mapped eagerly by exec; the rest grow on demand.
 *
 *   0x50000000  ← USTACK_BASE  (lowest page mapped initially)
 *   0x50001000  ← USTACK_TOP   (initial RSP = TOP - 16)
 *
 * The stack grows DOWN.  On every not-present write below USTACK_BASE the
 * page-fault handler maps a new page, down to USTACK_LIMIT (64 pages = 256 KB).
 */
#define USTACK_BASE   0x50000000ULL          /* VA of the eagerly-mapped page  */
#define USTACK_TOP    (USTACK_BASE + 0x1000ULL)  /* initial RSP anchor         */
#define USTACK_PAGES  64ULL                  /* maximum stack pages            */
#define USTACK_LIMIT  (USTACK_BASE - (USTACK_PAGES - 1ULL) * 0x1000ULL)

/* Heap (brk) grows UP from the end of the ELF image. */
#define USER_HEAP_MAX  0x40000000ULL

#endif /* MM_VIRT_UVM_H */
