/**
 * @file    src/userland64/cat.c
 * @brief   cat — prints the contents of the file named in argv[1].
 */
#include "../noxlib64/noxlib.h"

int main(int argc, char** argv) {
    if (argc < 2) { puts("usage: cat.elf <file>\n"); return 1; }
    long fd = open(argv[1], O_RDONLY);
    if (fd < 0) { puts("cat: cannot open "); puts(argv[1]); puts("\n"); return 1; }
    char b[256];
    ssize_t r;
    while ((r = read((int)fd, b, sizeof(b))) > 0) write(1, b, (size_t)r);
    close((int)fd);
    return 0;
}
