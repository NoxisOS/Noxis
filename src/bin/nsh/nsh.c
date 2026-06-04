/**
 * @file    src/bin/nsh/nsh.c
 * @brief   Noxis shell — readline, multi-stage pipes, scripts, background jobs.
 *
 * Builtins: cd, pwd, exit, time, help
 * Features: cmd1 | cmd2 | cmd3  (up to 8 stages)
 *           cmd &                (background)
 *           nsh script.sh        (execute script file)
 *           cmd > file, >> file, < file
 */
#include <lib/noxlib/noxlib.h>

#define MAX_ARGS    32
#define MAX_STAGES  8

/* ── Helpers ─────────────────────────────────────────────────────────── */

/* Try execv(argv[0]); if it fails and name has no '.', also try name.elf */
static void _exec(char** argv) {
    execv(argv[0], argv);
    int has_dot = 0;
    for (const char* p = argv[0]; *p; p++) if (*p == '.') { has_dot = 1; break; }
    if (!has_dot) {
        char buf[256];
        snprintf(buf, sizeof(buf), "%s.elf", argv[0]);
        execv(buf, argv);
    }
}

/* ── Parse ────────────────────────────────────────────────────────────── */

/*
 * Tokenise `line` into argv[].  Pulls out redirections.
 * Returns argc; sets *infile/*outfile (NULL if none) and *append.
 */
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
        if (*p) *p++ = 0;

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

/* ── Run a single command ─────────────────────────────────────────────── */

static void run_one(char** argv, char* infile, char* outfile,
                    int append, int background) {
    long pid = fork();
    if (pid == 0) {
        if (infile) {
            long fd = open(infile, O_RDONLY);
            if (fd < 0) { fprintf(2, "nsh: cannot open %s\n", infile); exit(1); }
            dup2((int)fd, 0); close((int)fd);
        }
        if (outfile) {
            long fd = open(outfile, O_WRONLY | O_CREAT | (append ? 0 : O_TRUNC));
            if (fd < 0) { fprintf(2, "nsh: cannot create %s\n", outfile); exit(1); }
            dup2((int)fd, 1); close((int)fd);
        }
        _exec(argv);
        fprintf(2, "%s: command not found\n", argv[0]);
        exit(127);
    }
    if (background) {
        printf("[1] %ld\n", pid);
    } else {
        int st = 0;
        setfg(pid);
        waitpid(pid, &st);
        setfg(0);
    }
}

/* ── Multi-stage pipeline ─────────────────────────────────────────────── */

/*
 * Split `line` on '|' (up to MAX_STAGES stages), NUL-terminating each.
 * Returns number of stages; fills stage_starts[].
 */
static int split_stages(char* line, char** stage_starts) {
    int n = 0;
    stage_starts[n++] = line;
    for (char* p = line; *p; p++) {
        if (*p == '|' && n < MAX_STAGES) {
            *p = 0;
            stage_starts[n++] = p + 1;
        }
    }
    return n;
}

static void run_pipeline(char* line) {
    char* stage_start[MAX_STAGES];
    int   n = split_stages(line, stage_start);

    /* Parse each stage into its own argv */
    char* sav[MAX_STAGES][MAX_ARGS];
    char* dummy_in[MAX_STAGES], *dummy_out[MAX_STAGES];
    int   dummy_app[MAX_STAGES];
    for (int i = 0; i < n; i++)
        parse(stage_start[i], sav[i],
              &dummy_in[i], &dummy_out[i], &dummy_app[i]);

    /* Create n-1 pipes */
    int fds[MAX_STAGES - 1][2];
    for (int i = 0; i < n - 1; i++) pipe(fds[i]);

    /* Fork all children */
    long pids[MAX_STAGES];
    for (int i = 0; i < n; i++) {
        pids[i] = fork();
        if (pids[i] == 0) {
            if (i > 0)     dup2(fds[i-1][0], 0);
            if (i < n - 1) dup2(fds[i][1],   1);
            for (int j = 0; j < n - 1; j++) {
                close(fds[j][0]); close(fds[j][1]);
            }
            if (sav[i][0]) { _exec(sav[i]); fprintf(2, "%s: not found\n", sav[i][0]); }
            exit(127);
        }
    }

    /* Parent closes all pipe ends */
    for (int i = 0; i < n - 1; i++) { close(fds[i][0]); close(fds[i][1]); }

    /* Wait for all stages */
    int st = 0;
    setfg(pids[0]);
    for (int i = 0; i < n; i++) waitpid(pids[i], &st);
    setfg(0);
}

/* ── Script / interactive line source ────────────────────────────────── */

/*
 * Read the next non-empty, non-comment line.
 * fd=-1 → readline (interactive); fd≥0 → read from script file.
 * Returns NULL on EOF.
 */
static char* next_line(int fd, const char* prompt, char* buf, int bufsz) {
    for (;;) {
        if (fd < 0) {
            /* Interactive */
            char* s = readline(prompt);
            if (!s) return (char*)0;
            int n = (int)strlen(s);
            if (n >= bufsz - 1) n = bufsz - 2;
            for (int i = 0; i < n; i++) buf[i] = s[i];
            buf[n] = 0;
        } else {
            /* Script file */
            int n = 0; char c;
            while (n < bufsz - 1) {
                if (read(fd, &c, 1) != 1) {
                    if (n == 0) return (char*)0;  /* EOF */
                    break;
                }
                if (c == '\n') break;
                if (c != '\r') buf[n++] = c;
            }
            buf[n] = 0;
            /* Strip inline comments */
            for (int i = 0; buf[i]; i++) if (buf[i] == '#') { buf[i] = 0; break; }
        }
        /* Skip blank lines */
        int blank = 1;
        for (int i = 0; buf[i]; i++) if (buf[i] != ' ' && buf[i] != '\t') { blank = 0; break; }
        if (!blank) return buf;
    }
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(int argc, char** argv) {
    int script_fd = -1;
    if (argc > 1) {
        script_fd = (int)open(argv[1], O_RDONLY);
        if (script_fd < 0) {
            fprintf(2, "nsh: %s: cannot open\n", argv[1]);
            return 1;
        }
    } else {
        puts("\nnsh \xe2\x80\x94 Noxis shell.  type 'help' for info.\n");
    }

    char  line[256];
    char* av[MAX_ARGS];
    char* in_f, *out_f; int app;

    for (;;) {
        /* Build prompt (interactive only) */
        char cwd[128], prompt[160];
        getcwd(cwd, sizeof(cwd));
        snprintf(prompt, sizeof(prompt), "nsh:%s$ ", cwd);

        /* Get next line */
        char* result = next_line(script_fd, prompt, line, sizeof(line));
        if (!result) break;   /* EOF (script done or Ctrl-D) */

        /* Count '|' to decide execution path */
        int pipe_count = 0;
        for (char* p = line; *p; p++) if (*p == '|') pipe_count++;

        if (pipe_count > 0) {
            run_pipeline(line);
            continue;
        }

        /* Single-command path */
        int ac = parse(line, av, &in_f, &out_f, &app);
        if (ac == 0) continue;

        /* Background flag */
        int background = 0;
        if (ac > 0 && strcmp(av[ac - 1], "&") == 0) {
            background = 1; av[--ac] = (char*)0;
        }
        if (ac == 0) continue;

        /* Builtins */
        if (strcmp(av[0], "exit") == 0) { puts("bye!\n"); return 0; }

        if (strcmp(av[0], "cd") == 0) {
            const char* dest = (ac > 1) ? av[1] : "/";
            if (chdir(dest) < 0) fprintf(2, "nsh: cd: %s: not found\n", dest);
            continue;
        }
        if (strcmp(av[0], "pwd") == 0) { puts(cwd); puts("\n"); continue; }

        if (strcmp(av[0], "time") == 0) {
            if (ac < 2) { puts("usage: time <cmd>\n"); continue; }
            unsigned long t0 = uptime_ms();
            run_one(av + 1, in_f, out_f, app, 0);
            printf("real\t%lums\n", uptime_ms() - t0);
            continue;
        }
        if (strcmp(av[0], "help") == 0) {
            puts("builtins:  cd, pwd, time, exit, help\n");
            puts("programs:  ls cat echo ps wc head tail grep sort mkdir rm mv\n");
            puts("           (works with or without .elf suffix)\n");
            puts("pipelines: cmd1 | cmd2 | cmd3  (up to 8 stages)\n");
            puts("redir:     > file   >> file   < file\n");
            puts("bg:        cmd &\n");
            puts("scripts:   nsh script.sh\n");
            continue;
        }

        run_one(av, in_f, out_f, app, background);
    }

    if (script_fd >= 0) close(script_fd);
    return 0;
}
