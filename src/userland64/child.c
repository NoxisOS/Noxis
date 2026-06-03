/**
 * @file    src/userland64/child.c
 * @brief   Tiny ring-3 program used to demonstrate exec(): prints and exits.
 */
#include "../noxlib64/noxlib.h"

int main(int argc, char** argv) {
    puts("[exec] child.elf running in ring 3, pid=");
    puti(getpid());
    puts(", argc="); puti(argc); puts("\n");
    for (int i = 0; i < argc; i++) {
        puts("  argv["); puti(i); puts("]="); puts(argv[i]); puts("\n");
    }
    return 42;
}
