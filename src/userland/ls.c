/**
 * @file    src/userland/ls.c
 * @brief   ls — lists the files in the VFS via the readdir syscall.
 */
#include "../noxlib/noxlib.h"

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    char name[32];
    for (long i = 0; readdir(i, name); i++) {
        puts(name);
        puts("\n");
    }
    return 0;
}
