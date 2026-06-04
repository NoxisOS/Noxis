/**
 * @file    src/bin/mkdir/mkdir.c
 * @brief   mkdir — create one or more directories.
 */
#include <lib/noxlib/noxlib.h>

int main(int argc, char** argv) {
    if (argc < 2) { puts("usage: mkdir <dir> [dir ...]\n"); return 1; }
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        if (mkdir(argv[i]) < 0) {
            puts("mkdir: cannot create "); puts(argv[i]); puts("\n");
            rc = 1;
        }
    }
    return rc;
}
