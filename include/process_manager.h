#ifndef PROCESS_MANAGER_H
#define PROCESS_MANAGER_H

#include <stddef.h>
#include <sys/types.h>

#define PROCESS_MANAGER_CHILD_COUNT 2

int process_manager_start(void);

void process_manager_handle_events(void);

void process_manager_begin_shutdown(void);

int process_manager_all_stopped(void);

void process_manager_kill_remaining(void);

size_t process_manager_get_status(
    pid_t pids[],
    const char *states[],
    size_t capacity
);

#endif

