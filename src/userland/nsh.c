/**
 * @file    userland/nsh.c
 * @brief   nsh — Noxis Shell
 *
 * A proper interactive shell compiled against noxlib.
 *
 * Features:
 *   Pipes:        cmd1 | cmd2 | cmd3
 *   Redirections: cmd > file   cmd >> file   cmd < file
 *   Background:   cmd &
 *   $? expansion in any argument
 *   Builtins:     cd  pwd  ls  echo  clear  exit  help
 *
 * External programs are launched with fork() + execv().
 * If the program name has no extension, nsh tries it as-is
 * and then with ".elf" appended automatically.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

/* ── Limits ─────────────────────────────────────────────── */
#define MAX_LINE    512
#define MAX_TOKENS  128
#define MAX_STAGES  8
#define MAX_ARGS    32
#define MAX_CWD     256

/* ── NoxFS on-disk dirent (32 bytes, packed) ─────────────── */
typedef struct {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[24];
} __attribute__((packed)) dirent_t;

#define DIRENT_SZ  32
#define FT_DIR     2

/* ── Pipeline structures ────────────────────────────────── */

typedef struct {
    char *argv[MAX_ARGS + 1];   /* NULL-terminated */
    int   argc;
    char *redir_in;             /* < filename  */
    char *redir_out;            /* > filename  */
    int   append;               /* 1 = >>      */
} stage_t;

typedef struct {
    stage_t stages[MAX_STAGES];
    int     nstages;
    int     background;
} pipeline_t;

/* ── Shell state ─────────────────────────────────────────── */
static char g_cwd[MAX_CWD] = "/";
static int  g_last_status  = 0;
static volatile int g_sigint_received = 0;

/* SIGINT handler: just set a flag — the shell stays alive. */
static void _sigint_handler(int sig)
{
    (void)sig;
    g_sigint_received = 1;
}

/* ═══════════════════════════════════════════════════════════
 * Tokenizer
 * ═══════════════════════════════════════════════════════════ */

static int tokenize(char *line, char **toks, int max)
{
    /* Strip trailing whitespace / newlines */
    int len = (int)strlen(line);
    while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r' ||
                       line[len-1] == ' '  || line[len-1] == '\t'))
        line[--len] = '\0';

    int   n = 0;
    char *p = line;

    while (n < max - 1 && *p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '#') break;

        /* Two-char operators */
        if (p[0] == '>' && p[1] == '>') { toks[n++] = ">>"; p += 2; continue; }

        /* Single-char operators */
        switch (*p) {
        case '|': toks[n++] = "|"; p++; continue;
        case '>': toks[n++] = ">"; p++; continue;
        case '<': toks[n++] = "<"; p++; continue;
        case '&': toks[n++] = "&"; p++; continue;
        default:  break;
        }

        /* Word token (in-place, null-terminated) */
        toks[n++] = p;
        while (*p && *p != ' ' && *p != '\t' &&
               *p != '|' && *p != '>' && *p != '<' &&
               *p != '&' && *p != '#')
            p++;
        if (*p == '#') { *p = '\0'; break; }
        if (*p)        *p++ = '\0';
    }
    toks[n] = NULL;
    return n;
}

/* ═══════════════════════════════════════════════════════════
 * Parser: tokens → pipeline
 * ═══════════════════════════════════════════════════════════ */

static char g_status_str[16];   /* holds the stringified $? */

static int parse(char **toks, int ntoks, pipeline_t *pl)
{
    memset(pl, 0, sizeof(*pl));
    if (ntoks == 0) return 0;

    snprintf(g_status_str, sizeof(g_status_str), "%d", g_last_status);

    int     si = 0;
    stage_t *st = &pl->stages[0];

    for (int i = 0; i < ntoks; i++) {
        char *t = toks[i];

        if (strcmp(t, "|") == 0) {
            st->argv[st->argc] = NULL;
            if (++si >= MAX_STAGES) { printf("nsh: too many pipes\n"); return -1; }
            st = &pl->stages[si];
        }
        else if (strcmp(t, ">") == 0 || strcmp(t, ">>") == 0) {
            if (i + 1 >= ntoks) { printf("nsh: missing filename after %s\n", t); return -1; }
            st->redir_out = toks[++i];
            st->append    = (t[1] == '>');
        }
        else if (strcmp(t, "<") == 0) {
            if (i + 1 >= ntoks) { printf("nsh: missing filename after <\n"); return -1; }
            st->redir_in = toks[++i];
        }
        else if (strcmp(t, "&") == 0) {
            pl->background = 1;
        }
        else {
            if (strcmp(t, "$?") == 0) t = g_status_str;
            if (st->argc < MAX_ARGS)  st->argv[st->argc++] = t;
        }
    }

    st->argv[st->argc] = NULL;
    pl->nstages = si + 1;

    if (pl->stages[0].argc == 0) { pl->nstages = 0; return 0; }
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * Built-in: ls
 * ═══════════════════════════════════════════════════════════ */

static int builtin_ls(void)
{
    /* fd=99 is invalid → kernel uses cwd_ino automatically.
       No offset pointer needed: sysenter path reads from offset 0. */
    char buf[DIRENT_SZ * 128];
    int n = getdents(99, buf, (int)sizeof(buf));
    if (n < 0) { printf("ls: cannot read directory\n"); return 1; }

    dirent_t *de  = (dirent_t *)buf;
    int       cnt = n / DIRENT_SZ;

    for (int i = 0; i < cnt; i++, de++) {
        if (de->inode == 0) continue;

        int  nl = de->name_len < 24 ? de->name_len : 24;
        char name[25];
        memcpy(name, de->name, nl);
        name[nl] = '\0';

        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

        if (de->file_type == FT_DIR)
            printf("  /%s/\n", (name[0] == '/') ? name + 1 : name);
        else
            printf("  %s\n", name);
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * Built-in dispatcher
 * Returns exit code (>= 0) if handled, -1 if not a builtin.
 * ═══════════════════════════════════════════════════════════ */

static int exec_builtin(stage_t *st)
{
    if (st->argc == 0) return 0;
    char *cmd = st->argv[0];

    /* ── cd ── */
    if (strcmp(cmd, "cd") == 0) {
        const char *raw = (st->argc > 1) ? st->argv[1] : "/";
        if (chdir(raw) < 0) { printf("cd: %s: no such directory\n", raw); return 1; }

        /* Normalise the target: strip any trailing '/' so the displayed
           path never carries one (which would desync the ".." pop). */
        char dir[MAX_CWD];
        int  dl = 0;
        while (raw[dl] && dl < MAX_CWD - 1) { dir[dl] = raw[dl]; dl++; }
        while (dl > 1 && dir[dl-1] == '/') dl--;   /* drop trailing slashes */
        dir[dl] = '\0';

        /* Keep g_cwd (the displayed path) tidy. */
        if (dir[0] == '/') {
            strncpy(g_cwd, dir, MAX_CWD - 1);
            g_cwd[MAX_CWD - 1] = '\0';
        } else if (strcmp(dir, "..") == 0) {
            /* Pop the last path component. */
            int l = (int)strlen(g_cwd);
            while (l > 1 && g_cwd[l-1] != '/') l--;
            if (l > 1) l--;            /* drop the separating '/' too */
            if (l == 0) l = 1;         /* never below root */
            g_cwd[l] = '\0';
        } else if (strcmp(dir, ".") != 0) {
            int l = (int)strlen(g_cwd);
            if (l > 1 && g_cwd[l-1] != '/') strncat(g_cwd, "/", MAX_CWD - l - 1);
            strncat(g_cwd, dir, MAX_CWD - (int)strlen(g_cwd) - 1);
        }
        return 0;
    }

    /* ── pwd ── */
    if (strcmp(cmd, "pwd") == 0) { printf("%s\n", g_cwd); return 0; }

    /* ── cat ── (works on regular files AND /proc, /dev synthetic files) */
    if (strcmp(cmd, "cat") == 0) {
        if (st->argc < 2) { printf("cat: missing file\n"); return 1; }
        for (int i = 1; i < st->argc; i++) {
            int fd = open(st->argv[i], O_RDONLY);
            if (fd < 0) { printf("cat: %s: not found\n", st->argv[i]); return 1; }
            char buf[256];
            int n;
            while ((n = (int)read(fd, buf, sizeof(buf))) > 0)
                write(STDOUT_FILENO, buf, n);
            close(fd);
        }
        return 0;
    }

    /* ── mkdir ── */
    if (strcmp(cmd, "mkdir") == 0) {
        if (st->argc < 2) { printf("mkdir: missing name\n"); return 1; }
        for (int i = 1; i < st->argc; i++) {
            if (mkdir(st->argv[i]) < 0)
                printf("mkdir: %s: failed\n", st->argv[i]);
        }
        return 0;
    }

    /* ── keymap ── (show or switch keyboard layout) */
    if (strcmp(cmd, "keymap") == 0) {
        if (st->argc < 2) {
            /* No arg: print /proc/keymap. */
            int fd = open("/proc/keymap", O_RDONLY);
            if (fd < 0) { printf("keymap: unavailable\n"); return 1; }
            char buf[128]; int n;
            while ((n = (int)read(fd, buf, sizeof(buf))) > 0)
                write(STDOUT_FILENO, buf, n);
            close(fd);
            return 0;
        }
        /* Switch: write the name to /dev/keymap. */
        int fd = open("/dev/keymap", O_RDONLY);   /* flags ignored by kernel */
        if (fd < 0) { printf("keymap: cannot open control file\n"); return 1; }
        write(fd, st->argv[1], (int)strlen(st->argv[1]));
        close(fd);
        printf("keymap: switched to %s\n", st->argv[1]);
        return 0;
    }

    /* ── ls ── */
    if (strcmp(cmd, "ls") == 0) return builtin_ls();

    /* ── echo ── */
    if (strcmp(cmd, "echo") == 0) {
        for (int i = 1; i < st->argc; i++) {
            if (i > 1) putchar(' ');
            printf("%s", st->argv[i]);
        }
        putchar('\n');
        return 0;
    }

    /* ── clear ── */
    if (strcmp(cmd, "clear") == 0) {
        /* ANSI clear-screen + cursor home */
        write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
        return 0;
    }

    /* ── exit ── */
    if (strcmp(cmd, "exit") == 0) {
        exit((st->argc > 1) ? atoi(st->argv[1]) : 0);
        return 0; /* unreachable */
    }

    /* ── help ── */
    if (strcmp(cmd, "help") == 0) {
        printf("nsh -- Noxis Shell\n\n");
        printf("Builtins:\n");
        printf("  cd [dir]      change directory\n");
        printf("  pwd           print working directory\n");
        printf("  cat <file>    print file (incl. /proc, /dev)\n");
        printf("  mkdir <dir>   create a directory\n");
        printf("  keymap [name] show / switch keyboard layout (us, fr)\n");
        printf("  ls            list current directory\n");
        printf("  echo [args]   print arguments  ($? = last exit code)\n");
        printf("  clear         clear screen\n");
        printf("  exit [n]      exit shell\n");
        printf("  help          this message\n\n");
        printf("Syntax:\n");
        printf("  cmd arg1 arg2       run command with arguments\n");
        printf("  cmd1 | cmd2         pipe stdout to stdin\n");
        printf("  cmd > file          redirect stdout (overwrite)\n");
        printf("  cmd >> file         redirect stdout (append)\n");
        printf("  cmd < file          redirect stdin\n");
        printf("  cmd &               run in background\n");
        return 0;
    }

    return -1;  /* not a builtin */
}

/* ═══════════════════════════════════════════════════════════
 * Child: set up pipe + redirection fds, then exec
 * ═══════════════════════════════════════════════════════════ */

static void child_exec(stage_t *st, int pipes[][2], int idx, int nstages)
{
    /* Wire up pipe ends */
    if (idx > 0)           dup2(pipes[idx-1][0], STDIN_FILENO);
    if (idx < nstages - 1) dup2(pipes[idx][1],   STDOUT_FILENO);

    /* Close all pipe fds we inherited */
    for (int j = 0; j < nstages - 1; j++) {
        close(pipes[j][0]);
        close(pipes[j][1]);
    }

    /* Input redirection */
    if (st->redir_in) {
        int fd = open(st->redir_in, O_RDONLY);
        if (fd < 0) { printf("nsh: %s: cannot open\n", st->redir_in); _exit(1); }
        dup2(fd, STDIN_FILENO);
        close(fd);
    }

    /* Output redirection */
    if (st->redir_out) {
        int fd;
        if (st->append) {
            fd = open(st->redir_out, O_RDWR);
            if (fd < 0) fd = creat(st->redir_out);
            else        lseek(fd, 0, SEEK_END);
        } else {
            fd = creat(st->redir_out);
        }
        if (fd < 0) { printf("nsh: %s: cannot open\n", st->redir_out); _exit(1); }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }

    /* Restore SIGINT to default so the child can be killed by Ctrl+C. */
    signal(SIGINT, SIG_DFL);

    /* Builtin in child (e.g. ls in a pipeline) */
    int r = exec_builtin(st);
    if (r >= 0) _exit(r);

    /* External program: exact name */
    execv(st->argv[0], st->argv);

    /* Retry with .elf suffix */
    char elf_name[64];
    snprintf(elf_name, sizeof(elf_name), "%s.elf", st->argv[0]);
    execv(elf_name, st->argv);

    printf("nsh: %s: command not found\n", st->argv[0]);
    _exit(127);
}

/* ═══════════════════════════════════════════════════════════
 * Pipeline execution
 * ═══════════════════════════════════════════════════════════ */

static int exec_pipeline(pipeline_t *pl)
{
    if (pl->nstages == 0) return 0;

    /* cd and exit must run in the parent process (no fork). */
    if (pl->nstages == 1) {
        char *cmd = pl->stages[0].argv[0];
        if (cmd && (strcmp(cmd, "cd")   == 0 ||
                    strcmp(cmd, "exit") == 0))
            return exec_builtin(&pl->stages[0]);
    }

    /* Single command, no redirections, no background → run builtin inline. */
    if (pl->nstages == 1 &&
        !pl->stages[0].redir_in  &&
        !pl->stages[0].redir_out &&
        !pl->background) {
        int r = exec_builtin(&pl->stages[0]);
        if (r >= 0) return r;
    }

    int n = pl->nstages;
    int pipes[MAX_STAGES - 1][2];
    pid_t pids[MAX_STAGES];

    /* Create inter-stage pipes */
    for (int i = 0; i < n - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            printf("nsh: pipe() failed\n");
            return 1;
        }
    }

    /* Fork each stage */
    for (int i = 0; i < n; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            printf("nsh: fork() failed\n");
            for (int j = 0; j < n - 1; j++) { close(pipes[j][0]); close(pipes[j][1]); }
            return 1;
        }
        if (pids[i] == 0)
            child_exec(&pl->stages[i], pipes, i, n);
    }

    /* Parent: close all pipe ends */
    for (int i = 0; i < n - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    /* Wait (or background) */
    int status = 0;
    if (!pl->background) {
        for (int i = 0; i < n; i++) {
            int s = waitpid(pids[i], NULL, 0);
            if (i == n - 1) status = s;
        }
    } else {
        printf("[%d] running in background\n", (int)pids[0]);
    }
    return status;
}

/* ═══════════════════════════════════════════════════════════
 * Main REPL
 * ═══════════════════════════════════════════════════════════ */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    /* nsh catches SIGINT so Ctrl+C kills children but not the shell. */
    signal(SIGINT, _sigint_handler);

    char      line[MAX_LINE];
    char     *toks[MAX_TOKENS];
    pipeline_t pl;

    printf("\nnsh -- Noxis Shell  (type 'help')\n\n");

    for (;;) {
        /* If a previous Ctrl+C interrupted fgets, just print a new prompt. */
        if (g_sigint_received) {
            g_sigint_received = 0;
            g_last_status = 130;   /* 128 + SIGINT */
            putchar('\n');
            continue;
        }

        printf("nsh %s > ", g_cwd);

        if (!fgets(line, sizeof(line), STDIN_FILENO)) {
            /* fgets returned NULL: either EOF (Ctrl+D) or EINTR (signal). */
            if (g_sigint_received) {
                g_sigint_received = 0;
                g_last_status = 130;
                putchar('\n');
                continue;   /* stay alive, show next prompt */
            }
            break;          /* real EOF → exit */
        }

        /* Skip blank lines */
        char *p = line;
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (!*p) continue;

        int ntoks = tokenize(line, toks, MAX_TOKENS);
        if (ntoks == 0) continue;

        if (parse(toks, ntoks, &pl) < 0) continue;
        if (pl.nstages == 0) continue;

        g_last_status = exec_pipeline(&pl);
    }

    return g_last_status;
}
