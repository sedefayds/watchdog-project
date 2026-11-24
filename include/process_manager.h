#ifndef PROCESS_MANAGER_H
#define PROCESS_MANAGER_H

#include <stddef.h>
#include <sys/types.h>

/* Number of worker processes supervised by the watchdog (assignment: >= 2). */
#define PROCESS_MANAGER_CHILD_COUNT 2

/* Maximum number of automatic restarts per worker. */
#define PROCESS_MANAGER_MAX_RESTARTS 3

/* Installs the SIGCHLD/SIGALRM handlers, starts the periodic timer and forks
   the workers. Returns 0 on success, -1 on failure. */
int process_manager_start(void);

/* Called repeatedly from the main loop: reaps terminated workers (waitpid),
   applies the restart policy and runs the periodic health check. */
void process_manager_handle_events(void);

/* Copies PID and state name of each worker (PID is 0 when no process exists).
   Returns the number of entries written. */
size_t process_manager_get_status(pid_t pids[], const char *states[],
                                  size_t max_count);

/* Stops further restarts and sends SIGTERM to every running worker. */
void process_manager_begin_shutdown(void);

/* Sends SIGKILL to every worker that is still running. */
void process_manager_kill_remaining(void);

/* Returns 1 when no worker process is running any more, otherwise 0. */
int process_manager_all_stopped(void);

#endif
