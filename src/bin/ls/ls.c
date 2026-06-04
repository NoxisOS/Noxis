/**
 * @file    src/bin/ls/ls.c
 * @brief   ls — list files in the current (or given) directory.
 */
#include <lib/noxlib/noxlib.h>

#define BUF_ENTRIES 128

int main(int argc, char** argv) {
    const char* path = (argc > 1) ? argv[1] : ".";

    dirent_t buf[BUF_ENTRIES];
    long n = getdents(path, buf, (long)sizeof(buf));
    if (n < 0) {
        puts("ls: cannot read directory\n");
        return 1;
    }

    long count = n / (long)sizeof(dirent_t);
    for (long i = 0; i < count; i++) {
        if (buf[i].inode == 0) continue;       /* deleted entry */
        if (buf[i].name[0] == '.') continue;   /* skip . and .. */
        puts(buf[i].name);
        if (buf[i].type == 2) puts("/");        /* directory indicator */
        puts("\n");
    }
    return 0;
}
