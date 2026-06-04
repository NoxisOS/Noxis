/**
 * @file    src/bin/nsh/nsh.c
 * @brief   Noxis shell — reads a line, tokenizes it, fork+execs the command.
 *
 * Built-ins: exit, help.  Everything else is launched as an external program
 * (e.g. "ls.elf", "echo.elf hi", "cat.elf out.txt").
 */
#include <lib/noxlib/noxlib.h>

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

/* Run "left | right": connect left's stdout to right's stdin via a pipe. */
static void run_pipe(char** lav, char** rav) {
    int fds[2];
    if (pipe(fds) < 0) { puts("nsh: pipe failed\n"); return; }

    long p1 = fork();
    if (p1 == 0) {
        dup2(fds[1], 1); close(fds[0]); close(fds[1]);
        execv(lav[0], lav); puts(lav[0]); puts(": not found\n"); exit(127);
    }
    long p2 = fork();
    if (p2 == 0) {
        dup2(fds[0], 0); close(fds[0]); close(fds[1]);
        execv(rav[0], rav); puts(rav[0]); puts(": not found\n"); exit(127);
    }
    close(fds[0]); close(fds[1]);
    int st = 0;
    setfg(p1);
    waitpid(p1, &st);
    waitpid(p2, &st);
    setfg(0);
}

/* Split `line` on a single '|'; returns the right half (NUL-terminating the
   left) or NULL if there is no pipe. */
static char* split_pipe(char* line) {
    for (char* p = line; *p; p++)
        if (*p == '|') { *p = 0; return p + 1; }
    return 0;
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
    setfg(pid);       /* Ctrl-C now routes SIGINT to the child */
    waitpid(pid, &st);
    setfg(0);         /* no foreground process while nsh reads the next line */
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    puts("\nnsh \xe2\x80\x94 Noxis shell (ring 3). builtins: exit, help.\n");

    char  line[256];
    char* av[MAX_ARGS];
    char* rav[MAX_ARGS];
    char* in; char* out; int app;
    for (;;) {
        char _cwd[128];
        getcwd(_cwd, sizeof(_cwd));
        puts("nsh:"); puts(_cwd); puts("$ ");
        ssize_t n = read(0, line, sizeof(line) - 1);
        if (n <= 0) continue;
        if (line[n - 1] == '\n') n--;
        line[n] = 0;

        char* rhs = split_pipe(line);
        if (rhs) {                           /* a single-stage pipeline */
            char *i2, *o2; int a2;
            if (parse(line, av, &in, &out, &app) == 0) continue;
            if (parse(rhs, rav, &i2, &o2, &a2) == 0) continue;
            run_pipe(av, rav);
            continue;
        }

        int ac = parse(line, av, &in, &out, &app);
        if (ac == 0) continue;
        if (strcmp(av[0], "exit") == 0) { puts("bye!\n"); return 0; }
        if (strcmp(av[0], "cd") == 0) {
            const char* dest = (ac > 1) ? av[1] : "/";
            if (chdir(dest) < 0) { puts("nsh: cd: "); puts(dest); puts(": not found\n"); }
            continue;
        }
        if (strcmp(av[0], "help") == 0) {
            puts("builtins: exit, cd, help\n");
            puts("external: ls.elf echo.elf cat.elf ps.elf\n");
            puts("redirection: cmd > file, cmd >> file, cmd < file\n");
            continue;
        }
        run(av, in, out, app);
    }
}
