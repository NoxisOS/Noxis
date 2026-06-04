/**
 * @file    src/bin/sort/sort.c
 * @brief   sort — sort lines of a file or stdin alphabetically.
 *          Usage: sort [-r] [file ...]
 *          -r: reverse order
 */
#include <lib/noxlib/noxlib.h>

#define SORT_MAX_LINES 512
#define SORT_LINE_MAX  256

static char* _lines[SORT_MAX_LINES];
static int   _nlines;

/* Read lines from fd into _lines[] using malloc.  Returns 0 on success. */
static int read_lines(int fd) {
    char buf[SORT_LINE_MAX];
    int pos = 0; char c;

    while (_nlines < SORT_MAX_LINES) {
        if (read(fd, &c, 1) != 1) {
            /* EOF — flush any buffered chars */
            if (pos > 0) {
                buf[pos] = 0;
                char* s = (char*)malloc((size_t)(pos + 1));
                if (!s) return -1;
                memcpy(s, buf, (size_t)(pos + 1));
                _lines[_nlines++] = s;
            }
            break;
        }
        if (c == '\r') continue;
        if (c == '\n') {
            buf[pos] = 0;
            char* s = (char*)malloc((size_t)(pos + 1));
            if (!s) return -1;
            memcpy(s, buf, (size_t)(pos + 1));
            _lines[_nlines++] = s;
            pos = 0;
        } else if (pos < SORT_LINE_MAX - 1) {
            buf[pos++] = c;
        }
    }
    return 0;
}

static void insertion_sort(int rev) {
    for (int i = 1; i < _nlines; i++) {
        char* key = _lines[i];
        int j = i - 1;
        while (j >= 0) {
            int cmp = strcmp(_lines[j], key);
            if (rev ? cmp < 0 : cmp > 0) { _lines[j + 1] = _lines[j]; j--; }
            else break;
        }
        _lines[j + 1] = key;
    }
}

int main(int argc, char** argv) {
    int rev = 0, first_file = 1;
    if (argc > 1 && strcmp(argv[1], "-r") == 0) { rev = 1; first_file = 2; }

    _nlines = 0;

    if (first_file >= argc) {
        read_lines(0);
    } else {
        for (int i = first_file; i < argc; i++) {
            long fd = open(argv[i], O_RDONLY);
            if (fd < 0) { fprintf(2, "sort: %s: cannot open\n", argv[i]); continue; }
            read_lines((int)fd);
            close((int)fd);
        }
    }

    insertion_sort(rev);

    for (int i = 0; i < _nlines; i++) {
        puts(_lines[i]); puts("\n");
        free(_lines[i]);
    }
    return 0;
}
