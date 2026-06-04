/**
 * @file    src/bin/head/head.c
 * @brief   head — print the first N lines of a file (default 10).
 *          Usage: head [-n N] [file ...]
 */
#include <lib/noxlib/noxlib.h>

static void head(int fd, int n) {
    char c; int lines = 0;
    while (lines < n && read(fd, &c, 1) == 1) {
        write(1, &c, 1);
        if (c == '\n') { lines++; }
    }
}

int main(int argc, char** argv) {
    int n = 10;
    int first_file = 1;

    if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'n') {
        n = atoi(argv[1] + 2);
        if (!n && argv[1][2] == 0 && argc > 2) { n = atoi(argv[2]); first_file = 3; }
        else first_file = 2;
    }
    if (n <= 0) n = 10;

    if (first_file >= argc) { head(0, n); return 0; }

    int rc = 0;
    for (int i = first_file; i < argc; i++) {
        long fd = open(argv[i], O_RDONLY);
        if (fd < 0) { fprintf(2, "head: %s: cannot open\n", argv[i]); rc = 1; continue; }
        if (argc - first_file > 1) printf("==> %s <==\n", argv[i]);
        head((int)fd, n);
        close((int)fd);
    }
    return rc;
}
