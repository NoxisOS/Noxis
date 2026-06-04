/**
 * @file    src/bin/cp/cp.c
 * @brief   cp — copy a file.
 *          Usage: cp <src> <dst>
 */
#include <lib/noxlib/noxlib.h>

int main(int argc, char** argv) {
    if (argc < 3) { puts("usage: cp <src> <dst>\n"); return 1; }

    long src = open(argv[1], O_RDONLY);
    if (src < 0) { fprintf(2, "cp: cannot open %s\n", argv[1]); return 1; }

    long dst = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC);
    if (dst < 0) {
        close((int)src);
        fprintf(2, "cp: cannot create %s\n", argv[2]);
        return 1;
    }

    char buf[512]; long n;
    while ((n = read((int)src, buf, sizeof(buf))) > 0)
        write((int)dst, buf, (size_t)n);

    close((int)src);
    close((int)dst);
    return 0;
}
