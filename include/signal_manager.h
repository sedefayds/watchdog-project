#ifndef SIGNAL_MANAGER_H
#define SIGNAL_MANAGER_H

int signal_manager_install(void);

int signal_manager_shutdown_requested(void);

int signal_manager_take_status_request(void);

#endif

