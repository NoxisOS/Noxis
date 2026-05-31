/**
 * @file    userland/ctest.c
 * @brief   noxlib validation — first real C program running in Noxis
 *
 * Tests: printf, malloc/free, string ops, sprintf, getpid.
 * Compile with NOXLIB_CFLAGS, link against crt0.o + noxlib.a.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    /* ── Basic output ──────────────────────────────────────── */
    printf("=== noxlib ctest ===\n");

    /* ── printf formats ────────────────────────────────────── */
    printf("decimal   : %d\n",   42);
    printf("negative  : %d\n",   -7);
    printf("hex       : 0x%x\n", 0xdeadbeef);
    printf("padded    : [%08x]\n", 255);
    printf("left-pad  : [%-10s]\n", "hi");
    printf("pointer   : %p\n",   (void *)0x400000);

    /* ── getpid ────────────────────────────────────────────── */
    printf("pid       : %d\n", (int)getpid());

    /* ── malloc / free ─────────────────────────────────────── */
    char *buf = malloc(128);
    if (!buf) {
        printf("FAIL: malloc returned NULL\n");
        return 1;
    }
    strcpy(buf, "hello from malloc");
    printf("malloc    : %s\n", buf);
    free(buf);

    /* ── calloc ────────────────────────────────────────────── */
    int *arr = calloc(4, sizeof(int));
    if (arr) {
        arr[0] = 10; arr[1] = 20; arr[2] = 30; arr[3] = 40;
        printf("calloc    : %d %d %d %d\n", arr[0], arr[1], arr[2], arr[3]);
        free(arr);
    }

    /* ── sprintf ───────────────────────────────────────────── */
    char tmp[64];
    sprintf(tmp, "noxis %d.%d", 0, 1);
    printf("sprintf   : %s\n", tmp);

    /* ── string ops ────────────────────────────────────────── */
    char s[32];
    strcpy(s, "nox");
    strcat(s, "is");
    printf("strcat    : %s  len=%d\n", s, (int)strlen(s));
    printf("strcmp    : %d\n", strcmp("abc", "abd"));
    printf("strchr    : %s\n", strchr("hello", 'l'));

    /* ── atoi ──────────────────────────────────────────────── */
    printf("atoi      : %d\n", atoi("  -123abc"));

    printf("=== OK ===\n");
    return 0;
}
