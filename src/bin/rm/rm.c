/**
 * @file    src/bin/rm/rm.c
 * @brief   rm — remove files or empty directories.
 */
#include <lib/noxlib/noxlib.h>

int main(int argc, char** argv) {
    if (argc < 2) { puts("usage: rm <file> [file ...]\n"); return 1; }
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        if (unlink(argv[i]) < 0) {
            puts("rm: cannot remove "); puts(argv[i]); puts("\n");
            rc = 1;
        }
    }
    return rc;
}
