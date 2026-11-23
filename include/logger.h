#ifndef LOGGER_H
#define LOGGER_H

#include <stddef.h>
#include <sys/types.h>

int logger_init(const char *log_path);

int logger_log_event(const char *message);

int logger_write_status(
    const pid_t pids[],
    const char *states[],
    size_t count
);

void logger_close(void);

#endif

