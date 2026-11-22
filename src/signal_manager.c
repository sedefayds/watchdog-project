#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stddef.h>
#include "signal_manager.h"

/* Signal handler tarafindan degistirilen bayraklar */
static volatile sig_atomic_t shutdown_requested = 0;
static volatile sig_atomic_t status_requested = 0;

/* Gelen sinyale gore ilgili bayragi ayarlar */
static void handle_signal(int signal_number)
{
    if (signal_number == SIGINT ||
        signal_number == SIGTERM)
    {
        shutdown_requested = 1;
    }
    else if (signal_number == SIGUSR1)
    {
        status_requested = 1;
    }
}

/* Sinyal yakalama mekanizmasini kurar */
int signal_manager_install(void)
{
    struct sigaction action = {0};

    action.sa_handler = handle_signal;

    if (sigemptyset(&action.sa_mask) == -1)
        return -1;

    sigaddset(&action.sa_mask, SIGINT);
    sigaddset(&action.sa_mask, SIGTERM);
    sigaddset(&action.sa_mask, SIGUSR1);

    action.sa_flags = 0;

    if (sigaction(SIGINT, &action, NULL) == -1)
        return -1;

    if (sigaction(SIGTERM, &action, NULL) == -1)
        return -1;

    if (sigaction(SIGUSR1, &action, NULL) == -1)
        return -1;

    return 0;
}

/* Kapatma istegi kontrolu */
int signal_manager_shutdown_requested(void)
{
    return shutdown_requested;
}


/* Durum raporu istegi kontrolu */
int signal_manager_status_requested(void)
{
    return status_requested;
}

/* Islenen durum raporu istegini temizler */
void signal_manager_clear_status_request(void)
{
    status_requested = 0;
}

