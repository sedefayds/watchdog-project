
#include <stdio.h>
#include <stddef.h>
#include <sys/types.h>

#include "process_manager.h"
#include "logger.h"

static int stopped = 0;

/* Logger test fonksiyonlari */

int logger_init(const char *log_path)
{
    printf("[TEST] Logger initialized: %s\n", log_path);
    return 0;
}

int logger_log_event(const char *message)
{
    printf("[LOG] %s\n", message);
    return 0;
}

int logger_write_status(
    const pid_t pids[],
    const char *states[],
    size_t count)
{
    printf("[STATUS REPORT]\n");

    for (size_t i = 0; i < count; i++)
    {
        printf("PID: %ld - State: %s\n",
               (long)pids[i], states[i]);
    }

    return 0;
}

void logger_close(void)
{
    printf("[TEST] Logger closed\n");
}

/* Process manager test fonksiyonlari */

int process_manager_start(void)
{
    stopped = 0;
    printf("[TEST] Child processes started\n");
    return 0;
}

void process_manager_handle_events(void)
{
    /* Testte gercek process olayi yok. */
}

void process_manager_begin_shutdown(void)
{
    printf("[TEST] SIGTERM sent to children\n");

    /* Bilerek stopped = 1 yapmiyoruz.
       Cocuklarin kapanmadigini simule ediyoruz. */
}

int process_manager_all_stopped(void)
{
    return stopped;
}

void process_manager_kill_remaining(void)
{
    printf("[TEST] SIGKILL sent to remaining children\n");
    stopped = 1;
}

size_t process_manager_get_status(
    pid_t pids[],
    const char *states[],
    size_t capacity)
{
    size_t count = capacity < 2 ? capacity : 2;

    for (size_t i = 0; i < count; i++)
    {
        pids[i] = (pid_t)(1000 + i);
        states[i] = "RUNNING";
    }

    return count;
}
