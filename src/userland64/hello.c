/**
 * @file    src/userland64/hello.c
 * @brief   A tiny interactive ring-3 program (proto-shell) — proves
 *          read + write syscalls and a userland REPL in 64-bit.
 */
#include "../noxlib64/noxlib.h"

static int streq(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

int main(void) {
    puts("\nNoxis 64-bit userland shell (ring 3)\n");
    puts("commands: hello, help, exit\n");

    char line[128];
    for (;;) {
        puts("nsh64> ");
        ssize_t n = read(0, line, sizeof(line) - 1);
        if (n <= 0) continue;
        if (line[n - 1] == '\n') n--;        /* strip newline */
        line[n] = 0;

        if (streq(line, "exit")) {
            puts("bye!\n");
            return 0;
        } else if (streq(line, "hello")) {
            puts("Hello from ring 3!\n");
        } else if (streq(line, "help")) {
            puts("commands: hello, help, exit\n");
        } else if (n > 0) {
            puts("you said: ");
            write(1, line, (size_t)n);
            puts("\n");
        }
    }
}
