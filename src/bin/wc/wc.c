/**
 * @file    src/bin/wc/wc.c
 * @brief   wc — count lines, words, and bytes.
 */
#include <lib/noxlib/noxlib.h>

static void count(int fd, const char* label) {
    long lines = 0, words = 0, bytes = 0;
    int in_word = 0;
    char c;
    while (read(fd, &c, 1) == 1) {
        bytes++;
        if (c == '\n') lines++;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') in_word = 0;
        else if (!in_word) { in_word = 1; words++; }
    }
    printf("%8ld %8ld %8ld", lines, words, bytes);
    if (label) { puts(" "); puts(label); }
    puts("\n");
}

int main(int argc, char** argv) {
    if (argc < 2) { count(0, (const char*)0); return 0; }
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        long fd = open(argv[i], O_RDONLY);
        if (fd < 0) { fprintf(2, "wc: %s: cannot open\n", argv[i]); rc = 1; continue; }
        count((int)fd, argv[i]);
        close((int)fd);
    }
    return rc;
}
