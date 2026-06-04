/**
 * @file    src/bin/nsh/nsh.c
 * @brief   Noxis shell — if/while/for, multi-pipe, scripts, background,
 *          $VAR expansion, export/env/unset.
 *
 * Builtins: cd, pwd, exit, export, env, unset, time, help
 */
#include <lib/noxlib/noxlib.h>

/* ── Constants ───────────────────────────────────────────────────────── */
#define MAX_ARGS    32
#define MAX_STAGES  8
#define SB_MAX      512     /* max lines in the script / compound buffer */
#define SB_LINE     256     /* max chars per line */

/* ── Script / compound-command buffer ───────────────────────────────── */
static char sb[SB_MAX][SB_LINE];   /* line store */
static int  g_last_status;         /* exit code of most recent command */

/* ── String helpers ──────────────────────────────────────────────────── */
static const char* _trim(const char* s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

/* Return 1 if the first token of `line` is exactly `kw`. */
static int is_kw(const char* line, const char* kw) {
    const char* t = _trim(line);
    int n = (int)strlen(kw);
    if (strncmp(t, kw, (size_t)n) != 0) return 0;
    char next = t[n];
    return !next || next == ' ' || next == '\t' || next == ';';
}

/* Copy the text that follows `kw` on the same line (strips leading `;`/space
 * and also strips trailing `; then` / `; do` suffixes). */
static void after_kw(const char* line, const char* kw, char* out, int outsz) {
    const char* t = _trim(line);
    t += strlen(kw);
    while (*t == ' ' || *t == '\t') t++;
    /* Copy up to ';' */
    int n = 0;
    while (*t && *t != ';' && n < outsz - 1) out[n++] = *t++;
    while (n > 0 && (out[n-1] == ' ' || out[n-1] == '\t')) n--;
    out[n] = 0;
}

/* Find matching `close_kw` starting from line `from`, handling nesting
 * via `open_kw`.  Returns index or `end` if not found. */
static int find_end(int from, int end, const char* open_kw, const char* close_kw) {
    int depth = 0;
    for (int i = from; i < end; i++) {
        if (is_kw(sb[i], open_kw))   depth++;
        else if (is_kw(sb[i], close_kw)) {
            if (depth == 0) return i;
            depth--;
        }
    }
    return end;
}

/* Find `done` (closes both `while` and `for`). */
static int find_done(int from, int end) {
    int depth = 0;
    for (int i = from; i < end; i++) {
        if (is_kw(sb[i], "while") || is_kw(sb[i], "for")) depth++;
        else if (is_kw(sb[i], "done")) {
            if (depth == 0) return i;
            depth--;
        }
    }
    return end;
}

/* ── _exec: try cmd then cmd.elf then PATH ───────────────────────────── */
static void _exec_search(char** argv) {
    execv(argv[0], argv);
    int has_slash = 0;
    for (const char* p = argv[0]; *p; p++) if (*p == '/') { has_slash = 1; break; }
    if (has_slash) return;
    char buf[256];
    snprintf(buf, sizeof(buf), "%s.elf", argv[0]);
    execv(buf, argv);
    const char* path = getenv("PATH");
    if (!path) return;
    const char* p = path;
    while (*p) {
        char dir[128]; int dl = 0;
        while (*p && *p != ':' && dl < 127) dir[dl++] = *p++;
        dir[dl] = 0; if (*p == ':') p++;
        if (!dl) continue;
        snprintf(buf, sizeof(buf), "%s/%s",     dir, argv[0]); execv(buf, argv);
        snprintf(buf, sizeof(buf), "%s/%s.elf", dir, argv[0]); execv(buf, argv);
    }
}

/* ── $VAR expansion ──────────────────────────────────────────────────── */
static void expand_vars(const char* in, char* out, int outsz) {
    int i = 0, o = 0;
    while (in[i] && o < outsz - 1) {
        if (in[i] == '$') {
            i++;
            char vn[32]; int vl = 0;
            while (in[i] && (in[i]=='_' || (in[i]>='a'&&in[i]<='z')
                             || (in[i]>='A'&&in[i]<='Z')
                             || (in[i]>='0'&&in[i]<='9')) && vl < 31)
                vn[vl++] = in[i++];
            vn[vl] = 0;
            if (vl > 0) {
                const char* val = getenv(vn);
                if (val) while (*val && o < outsz-1) out[o++] = *val++;
            }
        } else { out[o++] = in[i++]; }
    }
    out[o] = 0;
}

/* ── Parse ───────────────────────────────────────────────────────────── */
static int parse(char* line, char** argv, char** inf, char** outf, int* app) {
    *inf = *outf = 0; *app = 0; int n = 0;
    char* p = line;
    while (*p && n < MAX_ARGS - 1) {
        while (*p == ' ') *p++ = 0;
        if (!*p) break;
        char* tok = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = 0;
        if (strcmp(tok, "<") == 0) {
            while (*p == ' ') *p++ = 0;
            *inf = p; while (*p && *p != ' ') p++; if (*p) *p++ = 0;
        } else if (strcmp(tok, ">") == 0 || strcmp(tok, ">>") == 0) {
            *app = (tok[1] == '>');
            while (*p == ' ') *p++ = 0;
            *outf = p; while (*p && *p != ' ') p++; if (*p) *p++ = 0;
        } else { argv[n++] = tok; }
    }
    argv[n] = 0; return n;
}

/* ── Single command runner (returns exit status) ─────────────────────── */
static int run_one_cmd(char** argv, char* inf, char* outf, int app, int bg) {
    long pid = fork();
    if (pid == 0) {
        if (inf) {
            long fd = open(inf, O_RDONLY);
            if (fd < 0) { fprintf(2, "nsh: cannot open %s\n", inf); exit(1); }
            dup2((int)fd, 0); close((int)fd);
        }
        if (outf) {
            long fd = open(outf, O_WRONLY | O_CREAT | (app ? 0 : O_TRUNC));
            if (fd < 0) { fprintf(2, "nsh: cannot create %s\n", outf); exit(1); }
            dup2((int)fd, 1); close((int)fd);
        }
        _exec_search(argv);
        fprintf(2, "%s: command not found\n", argv[0]);
        exit(127);
    }
    if (bg) { printf("[1] %ld\n", pid); return 0; }
    int st = 0; setfg(pid); waitpid(pid, &st); setfg(0);
    return st;
}

/* ── Pipeline runner ─────────────────────────────────────────────────── */
static int run_pipeline(char* line) {
    char* ss[MAX_STAGES]; int n = 0;
    ss[n++] = line;
    for (char* p = line; *p; p++)
        if (*p == '|' && n < MAX_STAGES) { *p = 0; ss[n++] = p+1; }

    char* sav[MAX_STAGES][MAX_ARGS];
    char* di[MAX_STAGES], *dof[MAX_STAGES]; int da[MAX_STAGES];
    for (int i = 0; i < n; i++)
        parse(ss[i], sav[i], &di[i], &dof[i], &da[i]);

    int fds[MAX_STAGES-1][2];
    for (int i = 0; i < n-1; i++) pipe(fds[i]);

    long pids[MAX_STAGES];
    for (int i = 0; i < n; i++) {
        pids[i] = fork();
        if (pids[i] == 0) {
            if (i > 0)     dup2(fds[i-1][0], 0);
            if (i < n - 1) dup2(fds[i][1],   1);
            for (int j = 0; j < n-1; j++) { close(fds[j][0]); close(fds[j][1]); }
            if (sav[i][0]) { _exec_search(sav[i]); fprintf(2, "%s: not found\n", sav[i][0]); }
            exit(127);
        }
    }
    for (int i = 0; i < n-1; i++) { close(fds[i][0]); close(fds[i][1]); }
    int st = 0; setfg(pids[0]);
    for (int i = 0; i < n; i++) waitpid(pids[i], &st);
    setfg(0);
    return st;
}

/* ── exec1: execute one command string, return exit status ───────────── */
static int exec1(char* raw) {
    if (!raw || !*raw) return 0;
    char exp[SB_LINE]; expand_vars(raw, exp, sizeof(exp));
    const char* t = _trim(exp);
    if (!*t || *t == '#') return 0;

    /* Pipeline? */
    int has_pipe = 0;
    for (const char* p = t; *p; p++) if (*p == '|') { has_pipe = 1; break; }
    if (has_pipe) return run_pipeline(exp);

    char tmp[SB_LINE]; int ti = 0;
    while (t[ti]) { tmp[ti] = t[ti]; ti++; } tmp[ti] = 0;

    char* av[MAX_ARGS], *inf, *outf; int app;
    int ac = parse(tmp, av, &inf, &outf, &app);
    if (ac == 0) return 0;

    int bg = 0;
    if (ac > 0 && strcmp(av[ac-1], "&") == 0) { bg = 1; av[--ac] = 0; }
    if (ac == 0) return 0;

    /* Builtins */
    if (strcmp(av[0], "exit")   == 0) { exit(ac > 1 ? atoi(av[1]) : 0); }
    if (strcmp(av[0], "cd")     == 0) {
        const char* d = (ac > 1) ? av[1] : "/";
        return chdir(d) < 0 ? 1 : 0;
    }
    if (strcmp(av[0], "pwd")    == 0) { char cwd[128]; getcwd(cwd,sizeof(cwd)); puts(cwd); puts("\n"); return 0; }
    if (strcmp(av[0], "export") == 0) {
        for (int i = 1; i < ac; i++) {
            char key[32]; int ki = 0;
            while (av[i][ki] && av[i][ki] != '=' && ki < 31) { key[ki] = av[i][ki]; ki++; }
            key[ki] = 0;
            setenv(key, av[i][ki] == '=' ? av[i]+ki+1 : "", 1);
        }
        return 0;
    }
    if (strcmp(av[0], "unset")  == 0) { for (int i=1;i<ac;i++) unsetenv(av[i]); return 0; }
    if (strcmp(av[0], "env")    == 0) {
        char buf[160];
        for (long idx = 0; getenv_at(idx,buf,sizeof(buf)) >= 0; idx++) { puts(buf); puts("\n"); }
        return 0;
    }
    if (strcmp(av[0], "true")   == 0) return 0;
    if (strcmp(av[0], "false")  == 0) return 1;
    if (strcmp(av[0], "time")   == 0) {
        if (ac < 2) return 0;
        unsigned long t0 = uptime_ms();
        int st = run_one_cmd(av+1, inf, outf, app, 0);
        printf("real\t%lums\n", uptime_ms()-t0);
        return st;
    }

    return run_one_cmd(av, inf, outf, app, bg);
}

/* ── exec_block: execute lines sb[from..to-1] with control flow ─────── */
static int exec_block(int from, int to) {
    int i = from;
    while (i < to) {
        const char* ln = _trim(sb[i]);
        if (!*ln || *ln == '#') { i++; continue; }

        /* ── if COND; then … [else …] fi ── */
        if (is_kw(sb[i], "if")) {
            char cond[SB_LINE]; after_kw(sb[i], "if", cond, sizeof(cond));

            /* Find 'then' */
            int then_i = i + 1;
            if (!is_kw(sb[i], "then")) {  /* not inline */
                while (then_i < to && !is_kw(sb[then_i], "then")) then_i++;
            }
            int fi_i   = find_end(i+1, to, "if", "fi");
            int else_i = -1;
            /* Scan for else/elif at depth 0 between then and fi */
            {
                int d = 0;
                for (int j = then_i+1; j < fi_i; j++) {
                    if (is_kw(sb[j], "if"))   d++;
                    else if (is_kw(sb[j], "fi")) d--;
                    else if (d == 0 && (is_kw(sb[j], "else") || is_kw(sb[j], "elif"))) {
                        else_i = j; break;
                    }
                }
            }

            int cond_ok = (exec1(cond) == 0);
            if (cond_ok) {
                exec_block(then_i+1, else_i >= 0 ? else_i : fi_i);
            } else if (else_i >= 0) {
                if (is_kw(sb[else_i], "elif")) {
                    /* Treat elif as a nested if: splice into temp slot */
                    char elif_line[SB_LINE];
                    snprintf(elif_line, sizeof(elif_line), "if %s",
                             _trim(sb[else_i]) + 4 /* skip "elif" */);
                    char saved[SB_LINE];
                    for (int k = 0; k < SB_LINE; k++) saved[k] = sb[else_i][k];
                    for (int k = 0; k < SB_LINE; k++) sb[else_i][k] = elif_line[k];
                    exec_block(else_i, fi_i + 1);
                    for (int k = 0; k < SB_LINE; k++) sb[else_i][k] = saved[k];
                } else {
                    exec_block(else_i+1, fi_i);
                }
            }
            i = fi_i + 1;
        }

        /* ── while COND; do … done ── */
        else if (is_kw(sb[i], "while")) {
            char cond[SB_LINE]; after_kw(sb[i], "while", cond, sizeof(cond));
            int do_i   = i + 1;
            while (do_i < to && !is_kw(sb[do_i], "do")) do_i++;
            int done_i = find_done(i+1, to);

            while (exec1(cond) == 0)
                exec_block(do_i+1, done_i);

            i = done_i + 1;
        }

        /* ── for VAR in … ; do … done ── */
        else if (is_kw(sb[i], "for")) {
            const char* t2 = _trim(sb[i]) + 3;  /* skip "for" */
            while (*t2 == ' ') t2++;
            char var[32]; int vi = 0;
            while (*t2 && *t2 != ' ' && vi < 31) var[vi++] = *t2++;
            var[vi] = 0;
            while (*t2 == ' ') t2++;
            if (strncmp(t2, "in", 2) == 0) t2 += 2;
            while (*t2 == ' ') t2++;

            char vals[32][64]; int nv = 0;
            while (*t2 && *t2 != ';' && nv < 32) {
                while (*t2 == ' ') t2++;
                if (!*t2 || *t2 == ';') break;
                int ci = 0;
                while (*t2 && *t2 != ' ' && *t2 != ';' && ci < 63)
                    vals[nv][ci++] = *t2++;
                vals[nv][ci] = 0;
                if (ci > 0) nv++;
            }

            int do_i   = i + 1;
            while (do_i < to && !is_kw(sb[do_i], "do")) do_i++;
            int done_i = find_done(i+1, to);

            for (int vi2 = 0; vi2 < nv; vi2++) {
                setenv(var, vals[vi2], 1);
                exec_block(do_i+1, done_i);
            }

            i = done_i + 1;
        }

        /* Skip control keywords that are handled by their parent */
        else if (is_kw(sb[i], "then") || is_kw(sb[i], "else") ||
                 is_kw(sb[i], "elif") || is_kw(sb[i], "fi")   ||
                 is_kw(sb[i], "do")   || is_kw(sb[i], "done")) {
            i++;
        }

        /* Regular command */
        else {
            g_last_status = exec1(sb[i]);
            i++;
        }
    }
    return g_last_status;
}

/* ── Line source (interactive readline or script file) ──────────────── */
static char* next_line(int fd, const char* prompt, char* buf, int bufsz) {
    for (;;) {
        if (fd < 0) {
            char* s = readline(prompt);
            if (!s) return (char*)0;
            int n = (int)strlen(s);
            if (n >= bufsz-1) n = bufsz-2;
            for (int j = 0; j < n; j++) buf[j] = s[j];
            buf[n] = 0;
        } else {
            int n = 0; char c;
            while (n < bufsz-1) {
                if (read(fd, &c, 1) != 1) { if (n == 0) return (char*)0; break; }
                if (c == '\n') break;
                if (c != '\r') buf[n++] = c;
            }
            buf[n] = 0;
            for (int j = 0; buf[j]; j++) if (buf[j] == '#') { buf[j] = 0; break; }
        }
        int blank = 1;
        for (int j = 0; buf[j]; j++) if (buf[j] != ' ' && buf[j] != '\t') { blank=0; break; }
        if (!blank) return buf;
    }
}

/* Collect a compound command block from `fd` (or readline) into sb[from..],
 * returns the number of lines collected. */
static int collect_block(int fd, int from, const char* first_line) {
    int n = from;
    for (int j = 0; j < SB_LINE && first_line[j]; j++) sb[n][j] = first_line[j];
    sb[n][SB_LINE-1] = 0; n++;

    /* Determine what closes this block */
    const char* close_kw = is_kw(first_line, "if") ? "fi" : "done";
    int depth = 0;

    char buf[SB_LINE];
    for (;;) {
        char* got = next_line(fd, "> ", buf, sizeof(buf));
        if (!got) break;
        for (int j = 0; j < SB_LINE && got[j]; j++) sb[n][j] = got[j];
        sb[n][SB_LINE-1] = 0; n++;

        const char* t = _trim(got);
        /* Track nesting for if/while/for */
        if (is_kw(t, "if") || is_kw(t, "while") || is_kw(t, "for")) depth++;
        if (is_kw(t, close_kw)) {
            if (depth == 0) break;
            depth--;
        }
    }
    return n - from;
}

/* ── main ─────────────────────────────────────────────────────────────── */
int main(int argc, char** argv) {
    setenv("PATH",  "/",   0);
    setenv("HOME",  "/",   0);
    setenv("SHELL", "nsh", 0);

    int script_fd = -1;
    if (argc > 1) {
        script_fd = (int)open(argv[1], O_RDONLY);
        if (script_fd < 0) { fprintf(2, "nsh: %s: cannot open\n", argv[1]); return 1; }
        /* Script mode: load all lines, then exec_block */
        int n = 0; char buf[SB_LINE];
        while (n < SB_MAX) {
            char* got = next_line(script_fd, "", buf, sizeof(buf));
            if (!got) break;
            for (int j = 0; j < SB_LINE && got[j]; j++) sb[n][j] = got[j];
            sb[n][SB_LINE-1] = 0; n++;
        }
        close(script_fd);
        return exec_block(0, n);
    }

    puts("\nnsh \xe2\x80\x94 Noxis shell.  type 'help' for info.\n");

    char cwd[128], prompt[160], line[SB_LINE];
    for (;;) {
        getcwd(cwd, sizeof(cwd));
        snprintf(prompt, sizeof(prompt), "nsh:%s$ ", cwd);

        char* got = next_line(-1, prompt, line, sizeof(line));
        if (!got) break;

        const char* t = _trim(line);
        if (!*t) continue;

        /* Compound command: collect block then execute */
        if (is_kw(t, "if") || is_kw(t, "while") || is_kw(t, "for")) {
            int n = collect_block(-1, 0, line);
            exec_block(0, n);
            continue;
        }

        /* help builtin (interactive only) */
        if (strcmp(t, "help") == 0) {
            puts("builtins:  cd, pwd, exit, export, env, unset, time, true, false, help\n");
            puts("programs:  ls cat echo ps cp touch wc head tail grep sort seq\n");
            puts("           mkdir rm mv  (works with or without .elf)\n");
            puts("control:   if CMD; then ... [elif CMD; then ...] [else ...] fi\n");
            puts("           while CMD; do ... done\n");
            puts("           for VAR in v1 v2 ...; do ... done\n");
            puts("pipeline:  cmd1 | cmd2 | cmd3  (8 stages)\n");
            puts("redir:     > file   >> file   < file\n");
            puts("bg:        cmd &\n");
            puts("scripts:   nsh script.sh\n");
            puts("vars:      export KEY=val   env   unset KEY   $KEY\n");
            continue;
        }

        g_last_status = exec1(line);
    }
    return 0;
}
