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

#include <mm/virt/paging.h>

/* Highest user-stack address (exclusive): initial ESP starts here. */
#define USER_STACK_TOP    0xB0001000u

/* The single page mapped eagerly by exec_run — [USER_STACK_INIT, TOP). */
#define USER_STACK_INIT   0xB0000000u

/* Maximum stack size in pages (256 KB) and the resulting low limit. */
#define USER_STACK_PAGES  64u
#define USER_STACK_LIMIT  (USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE)

#endif /* MM_VIRT_UVM_H */
