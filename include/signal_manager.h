#ifndef SIGNAL_MANAGER_H
#define SIGNAL_MANAGER_H

int signal_manager_install(void);

int signal_manager_shutdown_requested(void);

int signal_manager_status_requested(void);

void signal_manager_clear_status_request(void);

#endif


