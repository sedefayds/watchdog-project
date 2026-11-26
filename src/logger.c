#define _POSIX_C_SOURCE 200809L

#include "logger.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define LOG_LINE_MAX  1024
#define TIMESTAMP_MAX 32


static int log_fd = -1;


static int write_all(const char *buf, size_t len)
{
    while (len > 0)
    {
        ssize_t written = write(log_fd, buf, len);

        if (written < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return -1;
        }

        buf += written;
        len -= (size_t)written;
    }

    return 0;
}


static int format_timestamp(char *out, size_t size)
{
    time_t now = time(NULL);
    struct tm tm_now;

    if (now == (time_t)-1)
    {
        return -1;
    }

    if (localtime_r(&now, &tm_now) == NULL)
    {
        return -1;
    }

    if (strftime(out, size, "%Y-%m-%d %H:%M:%S", &tm_now) == 0)
    {
        return -1;
    }

    return 0;
}


static int write_line(const char *text)
{
    char timestamp[TIMESTAMP_MAX];
    char line[LOG_LINE_MAX];
    int len;

    if (log_fd < 0)
    {
        errno = EBADF;
        return -1;
    }

    if (format_timestamp(timestamp, sizeof(timestamp)) == -1)
    {
        (void)snprintf(timestamp, sizeof(timestamp), "unknown-time");
    }

    len = snprintf(line, sizeof(line), "[%s] %s\n", timestamp, text);

    if (len < 0)
    {
        errno = EINVAL;
        return -1;
    }

    
    if ((size_t)len >= sizeof(line))
    {
        len = (int)sizeof(line) - 1;
        line[len - 1] = '\n';
    }

    return write_all(line, (size_t)len);
}

int logger_init(const char *log_path)
{
    if (log_path == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    if (log_fd >= 0)
    {
        return 0; 
    }

   
    log_fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);

    if (log_fd < 0)
    {
        return -1;
    }

    return 0;
}

int logger_log_event(const char *message)
{
    if (message == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    return write_line(message);
}

int logger_write_status(const pid_t pids[], const char *states[], size_t count)
{
    char text[LOG_LINE_MAX];

    if (count > 0 && (pids == NULL || states == NULL))
    {
        errno = EINVAL;
        return -1;
    }

    (void)snprintf(text, sizeof(text),
                   "STATUS REPORT begin (%zu workers)", count);
    if (write_line(text) == -1)
    {
        return -1;
    }

    for (size_t i = 0; i < count; ++i)
    {
        const char *state = (states[i] != NULL) ? states[i] : "unknown";

        (void)snprintf(text, sizeof(text),
                       "STATUS worker[%zu] pid=%ld state=%s",
                       i, (long)pids[i], state);
        if (write_line(text) == -1)
        {
            return -1;
        }
    }

    return write_line("STATUS REPORT end");
}

void logger_close(void)
{
    if (log_fd >= 0)
    {
        (void)close(log_fd);
        log_fd = -1;
    }
}