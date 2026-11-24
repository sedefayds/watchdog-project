#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t worker_stop = 0;

static void stop_handler(int signo)
{
    (void)signo;
    worker_stop = 1;
}

int main(int argc, char **argv)
{
    int id = 0;
    if (argc == 3 && strcmp(argv[1], "--id") == 0) id = atoi(argv[2]);

    const char *mode = getenv("WORKER_MODE");
    if (mode == NULL) mode = "run";
    if (strcmp(mode, "ignore-term") == 0) {
        /* Dedicated timeout-test mode. */
        struct sigaction ignore = {.sa_handler = SIG_IGN};
        sigemptyset(&ignore.sa_mask);
        if (sigaction(SIGTERM, &ignore, NULL) == -1) return 2;
    } else {
        struct sigaction sa = {.sa_handler = stop_handler};
        sigemptyset(&sa.sa_mask);
        if (sigaction(SIGTERM, &sa, NULL) == -1) return 2;
    }

    /* The watchdog starts us with SIGTERM blocked; the handler (or SIG_IGN) is
       installed now, so a SIGTERM that arrived during startup is delivered
       safely at this point. */
    sigset_t unblock;
    sigemptyset(&unblock);
    sigaddset(&unblock, SIGTERM);
    if (sigprocmask(SIG_UNBLOCK, &unblock, NULL) == -1) return 2;

    (void)printf("Worker %d started PID=%ld mode=%s\n", id, (long)getpid(), mode);
    (void)fflush(stdout);
    for (int seconds = 0; !worker_stop; ++seconds) {
        struct timespec one_second = {.tv_sec = 1, .tv_nsec = 0};
        while (nanosleep(&one_second, &one_second) == -1 && !worker_stop) {}
        if (strcmp(mode, "exit") == 0 && seconds >= 0) return 0;
        if (strcmp(mode, "fail") == 0 && seconds >= 0) return 7;
    }
    return 0;
}
