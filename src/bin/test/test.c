/**
 * @file    src/bin/test/test.c
 * @brief   test — evaluate conditional expressions.
 *
 * Usage: test EXPR   or   [ EXPR ]
 * Returns 0 (true) or 1 (false).
 *
 * Unary:   -f FILE   -d FILE   -e FILE   -r FILE
 *          -z STR    -n STR
 * Binary:  STR = STR    STR != STR
 *          NUM -eq NUM  -ne -lt -le -gt -ge
 * Negation: ! EXPR
 */
#include <lib/noxlib/noxlib.h>

static int _eval(int argc, char** argv);

int main(int argc, char** argv) {
    /* Support `[ ... ]` invocation: strip trailing `]` */
    if (argc > 1 && strcmp(argv[argc-1], "]") == 0) argc--;
    /* Skip program name */
    return _eval(argc - 1, argv + 1) ? 1 : 0;  /* 0=true, 1=false */
}

static int _eval(int argc, char** argv) {
    if (argc == 0) return 0;  /* empty → false */

    /* ! EXPR */
    if (strcmp(argv[0], "!") == 0)
        return !_eval(argc - 1, argv + 1);

    /* Unary: -X ARG */
    if (argc == 2 && argv[0][0] == '-') {
        const char* op  = argv[0];
        const char* arg = argv[1];
        if (strcmp(op, "-z") == 0) return strlen(arg) == 0;
        if (strcmp(op, "-n") == 0) return strlen(arg) != 0;
        stat_t st;
        int exists = (stat(arg, &st) == 0);
        if (strcmp(op, "-e") == 0) return exists;
        if (strcmp(op, "-f") == 0) return exists && S_ISREG(st.mode);
        if (strcmp(op, "-d") == 0) return exists && S_ISDIR(st.mode);
        if (strcmp(op, "-r") == 0) return exists;   /* simplification: exists ≈ readable */
        if (strcmp(op, "-s") == 0) return exists && st.size > 0;
        return 0;  /* unknown unary → false */
    }

    /* Binary: ARG OP ARG */
    if (argc == 3) {
        const char* a  = argv[0];
        const char* op = argv[1];
        const char* b  = argv[2];

        /* String comparisons */
        if (strcmp(op, "=")  == 0 || strcmp(op, "==") == 0) return strcmp(a, b) == 0;
        if (strcmp(op, "!=") == 0) return strcmp(a, b) != 0;
        if (strcmp(op, "<")  == 0) return strcmp(a, b) < 0;
        if (strcmp(op, ">")  == 0) return strcmp(a, b) > 0;

        /* Numeric comparisons */
        long av = atoi(a), bv = atoi(b);
        if (strcmp(op, "-eq") == 0) return av == bv;
        if (strcmp(op, "-ne") == 0) return av != bv;
        if (strcmp(op, "-lt") == 0) return av <  bv;
        if (strcmp(op, "-le") == 0) return av <= bv;
        if (strcmp(op, "-gt") == 0) return av >  bv;
        if (strcmp(op, "-ge") == 0) return av >= bv;
    }

    /* Single string: true if non-empty */
    if (argc == 1) return strlen(argv[0]) != 0;

    return 0;  /* unknown form → false */
}
