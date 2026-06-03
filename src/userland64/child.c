/**
 * @file    src/userland64/child.c
 * @brief   Tiny ring-3 program used to demonstrate exec(): prints and exits.
 */
#include "../noxlib64/noxlib.h"

int main(void) {
    puts("[exec] child.elf now running in ring 3, pid=");
    puti(getpid());
    puts("\n");
    return 42;
}
