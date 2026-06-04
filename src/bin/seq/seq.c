/**
 * @file    src/bin/seq/seq.c
 * @brief   seq — print a sequence of integers.
 *          Usage: seq [first [step]] last
 */
#include <lib/noxlib/noxlib.h>

int main(int argc, char** argv) {
    long first = 1, step = 1, last;
    if (argc == 2) {
        last = atoi(argv[1]);
    } else if (argc == 3) {
        first = atoi(argv[1]); last = atoi(argv[2]);
    } else if (argc == 4) {
        first = atoi(argv[1]); step = atoi(argv[2]); last = atoi(argv[3]);
    } else {
        puts("usage: seq [first [step]] last\n"); return 1;
    }
    if (step == 0) { puts("seq: step cannot be zero\n"); return 1; }
    for (long i = first; step > 0 ? i <= last : i >= last; i += step) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%ld\n", i);
        puts(buf);
    }
    return 0;
}
