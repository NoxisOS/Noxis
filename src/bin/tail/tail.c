/**
 * @file    src/bin/tail/tail.c
 * @brief   tail — print the last N lines of a file (default 10).
 *          Usage: tail [-n N] [file ...]
 *
 * Uses a circular buffer of N line pointers so memory is bounded.
 */
#include <lib/noxlib/noxlib.h>

#define TAIL_LINE_MAX  512
#define TAIL_LINES_MAX 100

/* Line buffers stored as two interleaved halves to avoid extra malloc. */
static char  _lines[TAIL_LINES_MAX][TAIL_LINE_MAX];
static int   _llen [TAIL_LINES_MAX];

static void tail(int fd, int n) {
    if (n <= 0 || n > TAIL_LINES_MAX) n = TAIL_LINES_MAX;

    int head = 0, count = 0;
    char c; int pos = 0;

    while (read(fd, &c, 1) == 1) {
        if (c == '\r') continue;
        if (c == '\n') {
            _lines[head][pos] = 0;
            _llen [head]      = pos;
            head  = (head + 1) % n;
            if (count < n) count++;
            pos = 0;
        } else if (pos < TAIL_LINE_MAX - 1) {
            _lines[head][pos++] = c;
        }
    }
    /* Handle last line without trailing newline */
    if (pos > 0) {
        _lines[head][pos] = 0;
        _llen [head]      = pos;
        head  = (head + 1) % n;
        if (count < n) count++;
    }

    /* Print: oldest first */
    int start = (head - count + n) % n;
    for (int i = 0; i < count; i++) {
        int idx = (start + i) % n;
        write(1, _lines[idx], (size_t)_llen[idx]);
        write(1, "\n", 1);
    }
}

int main(int argc, char** argv) {
    int n = 10, first_file = 1;

    if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'n') {
        if (argv[1][2]) { n = atoi(argv[1] + 2); first_file = 2; }
        else if (argc > 2) { n = atoi(argv[2]); first_file = 3; }
    }
    if (n <= 0) n = 10;

    if (first_file >= argc) { tail(0, n); return 0; }

    int rc = 0;
    for (int i = first_file; i < argc; i++) {
        long fd = open(argv[i], O_RDONLY);
        if (fd < 0) { fprintf(2, "tail: %s: cannot open\n", argv[i]); rc = 1; continue; }
        if (argc - first_file > 1) printf("==> %s <==\n", argv[i]);
        tail((int)fd, n);
        close((int)fd);
    }
    return rc;
}
