/* Minimal ANSI test — writes red text directly */
#include <unistd.h>
#include <string.h>

int main(void) {
    write(STDOUT_FILENO, "\x1b[31mRED TEXT\x1b[0m\n", 16);
    write(STDOUT_FILENO, "PLAIN TEXT\n", 10);
    return 0;
}
