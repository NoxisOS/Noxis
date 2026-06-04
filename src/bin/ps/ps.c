/**
 * @file    src/bin/ps/ps.c
 * @brief   ps — lists running processes via the procinfo syscall (/proc-style).
 */
#include <lib/noxlib/noxlib.h>

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    static const char st[] = { 'R', 'r', 'B', 'Z' };   /* ready,run,block,zombie */
    puts("  PID S NAME\n");
    procinfo_t pi;
    for (long i = 0; procinfo(i, &pi); i++) {
        puts("  "); puti(pi.pid);
        puts(" ");
        char s[2] = { (pi.state >= 0 && pi.state < 4) ? st[pi.state] : '?', 0 };
        puts(s);
        puts(" "); puts(pi.name); puts("\n");
    }
    return 0;
}
