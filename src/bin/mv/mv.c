/**
 * @file    src/bin/mv/mv.c
 * @brief   mv — rename or move a file/directory.
 */
#include <lib/noxlib/noxlib.h>

int main(int argc, char** argv) {
    if (argc != 3) { puts("usage: mv <src> <dst>\n"); return 1; }
    if (rename(argv[1], argv[2]) < 0) {
        puts("mv: cannot rename "); puts(argv[1]); puts(" to "); puts(argv[2]); puts("\n");
        return 1;
    }
    return 0;
}
