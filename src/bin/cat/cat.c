/**
 * @file    src/bin/cat/cat.c
 * @brief   cat — prints the contents of the file named in argv[1].
 */
#include "../../lib/noxlib/noxlib.h"

int main(int argc, char** argv) {
    char b[256];
    ssize_t r;
    if (argc < 2) {                         /* no file: copy stdin → stdout */
        while ((r = read(0, b, sizeof(b))) > 0) write(1, b, (size_t)r);
        return 0;
    }
    long fd = open(argv[1], O_RDONLY);
    if (fd < 0) { puts("cat: cannot open "); puts(argv[1]); puts("\n"); return 1; }
    while ((r = read((int)fd, b, sizeof(b))) > 0) write(1, b, (size_t)r);
    close((int)fd);
    return 0;
}
