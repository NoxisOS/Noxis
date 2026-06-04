/**
 * @file    src/bin/grep/grep.c
 * @brief   grep — print lines matching a substring pattern.
 *          Usage: grep [-i] <pattern> [file ...]
 *          -i: case-insensitive match
 */
#include <lib/noxlib/noxlib.h>

static char _lo(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static int match(const char* line, const char* pat, int ci) {
    if (!ci) return strstr(line, pat) != (const char*)0;
    /* case-insensitive: lowercase comparison */
    size_t plen = strlen(pat);
    for (; *line; line++) {
        const char* h = line; const char* p = pat; size_t n = plen;
        while (n && _lo(*h) == _lo(*p)) { h++; p++; n--; }
        if (!n) return 1;
    }
    return 0;
}

static int grep_fd(int fd, const char* pat, int ci, int show_name, const char* name) {
    char line[512]; int lpos = 0, found = 0;
    char c;
    while (read(fd, &c, 1) == 1) {
        if (c == '\n' || lpos == (int)sizeof(line) - 1) {
            line[lpos] = 0;
            if (match(line, pat, ci)) {
                if (show_name) { puts(name); puts(":"); }
                puts(line); puts("\n");
                found = 1;
            }
            lpos = 0;
        } else { line[lpos++] = c; }
    }
    if (lpos > 0) {
        line[lpos] = 0;
        if (match(line, pat, ci)) {
            if (show_name) { puts(name); puts(":"); }
            puts(line); puts("\n");
            found = 1;
        }
    }
    return found;
}

int main(int argc, char** argv) {
    int ci = 0, argi = 1;
    if (argi < argc && strcmp(argv[argi], "-i") == 0) { ci = 1; argi++; }
    if (argi >= argc) { puts("usage: grep [-i] <pattern> [file ...]\n"); return 2; }

    const char* pat = argv[argi++];
    int found = 0, rc = 0;
    int show_name = (argc - argi) > 1;

    if (argi >= argc) {
        found = grep_fd(0, pat, ci, 0, (const char*)0);
    } else {
        for (int i = argi; i < argc; i++) {
            long fd = open(argv[i], O_RDONLY);
            if (fd < 0) { fprintf(2, "grep: %s: cannot open\n", argv[i]); rc = 2; continue; }
            if (grep_fd((int)fd, pat, ci, show_name, argv[i])) found = 1;
            close((int)fd);
        }
    }
    return found ? 0 : (rc ? rc : 1);
}
