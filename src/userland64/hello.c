/**
 * @file    src/userland64/hello.c
 * @brief   A C ring-3 program — proves the 64-bit userland C toolchain.
 */
#include "../noxlib64/noxlib.h"

int main(void) {
    puts("Hello from a C ring-3 program (64-bit)!\n");
    puts("noxlib64: write/exit syscalls work.\n");
    return 42;
}
