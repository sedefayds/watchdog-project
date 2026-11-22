#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "signal_manager.h"

int main(void)
{
    if (signal_manager_install() == -1)
    {
        perror("Signal installation failed");
        return EXIT_FAILURE;
    }

    printf("Test PID: %ld\n", (long)getpid());
    printf("Waiting for signals...\n");

    while (!signal_manager_shutdown_requested())
    {
        if (signal_manager_status_requested())
        {
            printf("SIGUSR1 received!\n");
            signal_manager_clear_status_request();
        }

        sleep(1);
    }

    printf("Shutdown signal received!\n");

    return EXIT_SUCCESS;
}
