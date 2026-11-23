
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
        int status = signal_manager_take_status_request();

        if (status == -1)
        {
            perror("Status request failed");
            return EXIT_FAILURE;
        }

        if (status == 1)
        {
            printf("SIGUSR1 received!\n");
        }

        sleep(1);
    }

    printf("Shutdown signal received!\n");

    return EXIT_SUCCESS;
}
