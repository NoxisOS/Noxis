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

    /* One-shot fork+exec demo at startup (runs without keyboard input). */
    puts("[demo] parent pid="); puti(getpid()); puts("\n");
    long dpid = fork();
    if (dpid == 0) {
        puts("[demo] child pid="); puti(getpid());
        puts(", exec child.elf...\n");
        execv("child.elf");                 /* replaces this image */
        puts("[demo] exec FAILED\n");       /* only reached on error */
        exit(1);
    } else {
        int st = -1;
        long w = waitpid(dpid, &st);
        puts("[demo] parent reaped child "); puti(w);
        puts(", status="); puti(st); puts("\n");
    }

    /* open/read/close demo: cat /motd.txt. */
    long fd = open("motd.txt", O_RDONLY);
    if (fd >= 0) {
        puts("[demo] cat motd.txt (fd="); puti(fd); puts("):\n");
        char fb[256]; ssize_t r;
        while ((r = read((int)fd, fb, sizeof(fb))) > 0) write(1, fb, (size_t)r);
        close((int)fd);
    } else {
        puts("[demo] open motd.txt failed\n");
    }

    puts("commands: hello, help, fork, pid, cat, exit\n");

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
            puts("commands: hello, help, fork, pid, cat, exit\n");
        } else if (streq(line, "pid")) {
            puts("my pid = "); puti(getpid()); puts("\n");
        } else if (streq(line, "cat")) {
            long fd = open("motd.txt", O_RDONLY);
            if (fd < 0) { puts("cat: motd.txt not found\n"); }
            else {
                char fb[256]; ssize_t r;
                while ((r = read((int)fd, fb, sizeof(fb))) > 0) write(1, fb, (size_t)r);
                close((int)fd);
            }
        } else if (streq(line, "fork")) {
            long pid = fork();
            if (pid == 0) {
                puts("  [child]  hello from the forked child, pid=");
                puti(getpid()); puts("\n");
                exit(7);
            } else {
                puts("  [parent] forked child pid="); puti(pid); puts("\n");
                int st = -1;
                long w = waitpid(pid, &st);
                puts("  [parent] child "); puti(w);
                puts(" exited, status="); puti(st); puts("\n");
            }
        } else if (n > 0) {
            puts("you said: ");
            write(1, line, (size_t)n);
            puts("\n");
        }
    }
}
