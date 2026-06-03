/**
 * @file    src/userland64/nsh.c
 * @brief   Noxis shell — reads a line, tokenizes it, fork+execs the command.
 *
 * Built-ins: exit, help.  Everything else is launched as an external program
 * (e.g. "ls.elf", "echo.elf hi", "cat.elf motd.txt").
 */
#include "../noxlib64/noxlib.h"

#define MAX_ARGS 32

/* Split `line` into argv, pulling out `< in`, `> out`, `>> out` redirections.
   Returns argc; sets *infile/*outfile (NULL if none) and *append. */
static int parse(char* line, char** argv, char** infile,
                 char** outfile, int* append) {
    *infile = *outfile = 0; *append = 0;
    int n = 0;
    char* p = line;
    while (*p && n < MAX_ARGS - 1) {
        while (*p == ' ') *p++ = 0;
        if (!*p) break;
        char* tok = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = 0;                    /* terminate token */

        if (strcmp(tok, "<") == 0) {
            while (*p == ' ') *p++ = 0;
            *infile = p; while (*p && *p != ' ') p++; if (*p) *p++ = 0;
        } else if (strcmp(tok, ">") == 0 || strcmp(tok, ">>") == 0) {
            *append = (tok[1] == '>');
            while (*p == ' ') *p++ = 0;
            *outfile = p; while (*p && *p != ' ') p++; if (*p) *p++ = 0;
        } else {
            argv[n++] = tok;
        }
    }
    argv[n] = 0;
    return n;
}

static void run(char** argv, char* infile, char* outfile, int append) {
    long pid = fork();
    if (pid == 0) {
        if (infile) {
            long fd = open(infile, O_RDONLY);
            if (fd < 0) { puts("nsh: cannot open "); puts(infile); puts("\n"); exit(1); }
            dup2((int)fd, 0); close((int)fd);
        }
        if (outfile) {
            long fd = open(outfile, O_WRONLY | O_CREAT | (append ? 0 : O_TRUNC));
            if (fd < 0) { puts("nsh: cannot create "); puts(outfile); puts("\n"); exit(1); }
            dup2((int)fd, 1); close((int)fd);
        }
        execv(argv[0], argv);
        puts(argv[0]); puts(": command not found\n");
        exit(127);
    }
    int st = 0;
    waitpid(pid, &st);
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    puts("\nnsh \xe2\x80\x94 Noxis shell (ring 3). builtins: exit, help.\n");

    /* Startup self-test so behaviour is visible without keyboard input,
       including a redirection: write echo output to a file, then cat it. */
    { char* a[] = { "ls.elf", 0 };                            run(a, 0, 0, 0); }
    { char* a[] = { "echo.elf", "hello", "from", "nsh", 0 };   run(a, 0, 0, 0); }
    { char* a[] = { "echo.elf", "written", "via", ">", 0 };    run(a, 0, "out.txt", 0); }
    { char* a[] = { "cat.elf", "out.txt", 0 };                 run(a, 0, 0, 0); }

    char  line[256];
    char* av[MAX_ARGS];
    char* in; char* out; int app;
    for (;;) {
        puts("nsh$ ");
        ssize_t n = read(0, line, sizeof(line) - 1);
        if (n <= 0) continue;
        if (line[n - 1] == '\n') n--;
        line[n] = 0;

        int ac = parse(line, av, &in, &out, &app);
        if (ac == 0) continue;
        if (strcmp(av[0], "exit") == 0) { puts("bye!\n"); return 0; }
        if (strcmp(av[0], "help") == 0) {
            puts("builtins: exit, help. external: ls.elf echo.elf cat.elf\n");
            puts("redirection: cmd > file, cmd >> file, cmd < file\n");
            continue;
        }
        run(av, in, out, app);
    }
}
