/**
 * @file    src/userland64/sigtest.c
 * @brief   Demonstrates signals: a child installs a SIGUSR1 handler and loops;
 *          the parent sends SIGUSR1; the handler runs and the child exits.
 */
#include "../noxlib64/noxlib.h"

static volatile int got = 0;

static void on_usr1(int s) {
    (void)s;
    puts("[sig] child handler ran (SIGUSR1)\n");
    got = 1;
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    signal(SIGUSR1, on_usr1);            /* inherited across fork */

    long pid = fork();
    if (pid == 0) {
        while (!got) getpid();           /* syscalls give delivery points */
        puts("[sig] child caught signal, exiting\n");
        exit(0);
    }

    for (volatile long i = 0; i < 3000000; i++) { }   /* let the child spin up */
    puts("[sig] parent sending SIGUSR1\n");
    kill(pid, SIGUSR1);

    int st = 0;
    waitpid(pid, &st);
    puts("[sig] parent reaped child\n");
    return 0;
}
