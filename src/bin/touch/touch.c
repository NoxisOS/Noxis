/**
 * @file    src/bin/touch/touch.c
 * @brief   touch — create empty files or update them (no timestamp support).
 *          Usage: touch <file> [file ...]
 */
#include <lib/noxlib/noxlib.h>

int main(int argc, char** argv) {
    if (argc < 2) { puts("usage: touch <file> [file ...]\n"); return 1; }
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        long fd = open(argv[i], O_WRONLY | O_CREAT);
        if (fd < 0) { fprintf(2, "touch: %s: failed\n", argv[i]); rc = 1; continue; }
        close((int)fd);
    }
    return rc;
}
