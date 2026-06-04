/**
 * @file    src/bin/echo/echo.c
 * @brief   echo — prints its arguments separated by spaces.
 */
#include "../../lib/noxlib/noxlib.h"

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        puts(argv[i]);
        if (i < argc - 1) puts(" ");
    }
    puts("\n");
    return 0;
}
