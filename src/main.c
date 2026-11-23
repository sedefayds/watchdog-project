
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "signal_manager.h"
#include "process_manager.h"
#include "logger.h"

#define LOG_FILE "watchdog.log"
#define SHUTDOWN_TIMEOUT_SECONDS 5

/* Kucuk bir sure bekler */
static void sleep_briefly(void)
{
    struct timespec delay = {
        .tv_sec = 0,
        .tv_nsec = 100000000L
    };

    /* Sinyal gelirse bekleme erken kesilebilir. */
    (void)nanosleep(&delay, NULL);
}

/* Log dosyasina olay yazar */
static void log_event(const char *message)
{
    if (logger_log_event(message) == -1)
    {
        perror("logger_log_event");
    }
}

/* Verilen zamanin asilip asilmadigini kontrol eder */
static int deadline_reached(const struct timespec *deadline)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) == -1)
    {
        return -1;
    }

    if (now.tv_sec > deadline->tv_sec ||
        (now.tv_sec == deadline->tv_sec &&
         now.tv_nsec >= deadline->tv_nsec))
    {
        return 1;
    }

    return 0;
}

/* SIGUSR1 geldiginde process durumlarini raporlar */
static void handle_status_request(void)
{
    pid_t pids[PROCESS_MANAGER_CHILD_COUNT];
    const char *states[PROCESS_MANAGER_CHILD_COUNT];

    signal_manager_clear_status_request();

    size_t count = process_manager_get_status(
        pids,
        states,
        PROCESS_MANAGER_CHILD_COUNT
    );

    if (logger_write_status(pids, states, count) == -1)
    {
        perror("logger_write_status");
    }
    else
    {
        log_event("Status report generated after SIGUSR1");
    }
}

/* Child processleri guvenli sekilde kapatir */
static void shutdown_children(void)
{
    struct timespec deadline;

    log_event("Shutdown initiated");

    /* Yeni process baslatilmasini durdur ve SIGTERM gonder */
    process_manager_begin_shutdown();

    if (clock_gettime(CLOCK_MONOTONIC, &deadline) == -1)
    {
        perror("clock_gettime");

        log_event("Shutdown timer failed; sending SIGKILL");
        process_manager_kill_remaining();
    }
    else
    {
        deadline.tv_sec += SHUTDOWN_TIMEOUT_SECONDS;

        /* Child processlerin kapanmasini bekle */
        while (!process_manager_all_stopped())
        {
            process_manager_handle_events();

            if (process_manager_all_stopped())
            {
                break;
            }

            int timeout = deadline_reached(&deadline);

            if (timeout == -1)
            {
                perror("clock_gettime");
                process_manager_kill_remaining();
                break;
            }

            if (timeout == 1)
            {
                log_event("Shutdown timeout expired; sending SIGKILL");
                process_manager_kill_remaining();
                break;
            }

            sleep_briefly();
        }
    }

    /* Kalan child processlerin sonlanmasini ve toplanmasini bekle */
    while (!process_manager_all_stopped())
    {
        process_manager_handle_events();

        if (!process_manager_all_stopped())
        {
            sleep_briefly();
        }
    }

    log_event("All child processes stopped");
}

int main(void)
{
    int exit_status = EXIT_SUCCESS;

    /* 1. Sinyal mekanizmasini kur */
    if (signal_manager_install() == -1)
    {
        perror("signal_manager_install");
        return EXIT_FAILURE;
    }

    /* 2. Log dosyasini baslat */
    if (logger_init(LOG_FILE) == -1)
    {
        perror("logger_init");
        return EXIT_FAILURE;
    }

    log_event("Watchdog started");

    /* 3. Child processleri baslat */
    if (process_manager_start() == -1)
    {
        log_event("Failed to start child processes");
        exit_status = EXIT_FAILURE;
        goto shutdown;
    }

    /* 4. Ana kontrol dongusu */
    while (!signal_manager_shutdown_requested())
    {
        /* Child process olaylarini kontrol et */
        process_manager_handle_events();

        if (signal_manager_shutdown_requested())
        {
            break;
        }

        /* SIGUSR1 geldiyse durum raporu olustur */
        if (signal_manager_status_requested())
        {
            handle_status_request();
        }

        sleep_briefly();
    }

shutdown:

    /* 5. Guvenli kapatma */
    shutdown_children();

    log_event("Watchdog stopped");

    logger_close();

    return exit_status;
}

