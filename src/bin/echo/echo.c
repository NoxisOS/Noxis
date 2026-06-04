/**
 * @file    src/bin/echo/echo.c
 * @brief   echo — print arguments.
 *          -n : no trailing newline
 *          -e : interpret \n \t \\ \r escapes
 */
#include <lib/noxlib/noxlib.h>

static void echo_str(const char* s, int escape) {
    if (!escape) { puts(s); return; }
    while (*s) {
        if (*s == '\\' && s[1]) {
            s++;
            switch (*s) {
            case 'n':  write(1, "\n", 1); break;
            case 't':  write(1, "\t", 1); break;
            case 'r':  write(1, "\r", 1); break;
            case '\\': write(1, "\\", 1); break;
            default:   write(1, "\\", 1); write(1, s, 1); break;
            }
        } else { write(1, s, 1); }
        s++;
    }
}

int main(int argc, char** argv) {
    int newline = 1, escape = 0, first_arg = 1;

    /* Parse flags */
    while (first_arg < argc && argv[first_arg][0] == '-') {
        const char* f = argv[first_arg] + 1;
        int is_flag = 1;
        for (int i = 0; f[i]; i++) {
            if (f[i] != 'n' && f[i] != 'e') { is_flag = 0; break; }
        }
        if (!is_flag) break;
        for (int i = 0; f[i]; i++) {
            if (f[i] == 'n') newline = 0;
            if (f[i] == 'e') escape  = 1;
        }
        first_arg++;
    }

    for (int i = first_arg; i < argc; i++) {
        if (i > first_arg) write(1, " ", 1);
        echo_str(argv[i], escape);
    }
    if (newline) write(1, "\n", 1);
    return 0;
}
