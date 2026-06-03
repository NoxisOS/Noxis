/**
 * @file    src/userland64/nsh.c
 * @brief   Noxis shell — reads a line, tokenizes it, fork+execs the command.
 *
 * Built-ins: exit, help.  Everything else is launched as an external program
 * (e.g. "ls.elf", "echo.elf hi", "cat.elf motd.txt").
 */
#include "../noxlib64/noxlib.h"

#define MAX_ARGS 32

static int tokenize(char* line, char** argv) {
    int n = 0;
    char* p = line;
    while (*p && n < MAX_ARGS - 1) {
        while (*p == ' ') *p++ = 0;          /* skip/cut leading spaces */
        if (!*p) break;
        argv[n++] = p;
        while (*p && *p != ' ') p++;
    }
    argv[n] = 0;
    return n;
}

static void run(char** argv) {
    long pid = fork();
    if (pid == 0) {
        execv(argv[0], argv);                /* replaces image on success */
        puts(argv[0]); puts(": command not found\n");
        exit(127);
    }
    int st = 0;
    waitpid(pid, &st);
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    puts("\nnsh \xe2\x80\x94 Noxis shell (ring 3). builtins: exit, help.\n");

    /* Startup self-test so behaviour is visible without keyboard input. */
    { char* a[] = { "ls.elf", 0 };                         run(a); }
    { char* a[] = { "echo.elf", "hello", "from", "nsh", 0 }; run(a); }
    { char* a[] = { "cat.elf", "motd.txt", 0 };            run(a); }

    char  line[256];
    char* av[MAX_ARGS];
    for (;;) {
        puts("nsh$ ");
        ssize_t n = read(0, line, sizeof(line) - 1);
        if (n <= 0) continue;
        if (line[n - 1] == '\n') n--;
        line[n] = 0;

        int ac = tokenize(line, av);
        if (ac == 0) continue;
        if (strcmp(av[0], "exit") == 0) { puts("bye!\n"); return 0; }
        if (strcmp(av[0], "help") == 0) {
            puts("builtins: exit, help. external: ls.elf echo.elf cat.elf\n");
            continue;
        }
        run(av);
    }
}
