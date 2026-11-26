#define _POSIX_C_SOURCE 200809L
#include "process_manager.h"
#include "logger.h"
#include "signal_manager.h"

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#define PERIODIC_CHECK_SECONDS 1

typedef enum {
    WORKER_NOT_STARTED,
    WORKER_RUNNING,
    WORKER_EXITED,
    WORKER_FAILED,
    WORKER_STOPPED
} worker_state_t;

typedef struct {
    int id;
    pid_t pid;
    worker_state_t state;
    int restarts;
} worker_t;

static worker_t workers[PROCESS_MANAGER_CHILD_COUNT];
static bool shutting_down = false;

static volatile sig_atomic_t child_event = 0;
static volatile sig_atomic_t timer_event = 0;

static void log_msg(const char *format, ...) __attribute__((format(printf, 1, 2)));

static void log_msg(const char *format, ...)
{
    char message[256];
    va_list args;
    va_start(args, format);
    int written = vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    if (written < 0) return;
    (void)logger_log_event(message);
}

static const char *state_name(worker_state_t state)
{
    switch (state) {
    case WORKER_NOT_STARTED: return "NOT_STARTED";
    case WORKER_RUNNING: return "RUNNING";
    case WORKER_EXITED: return "EXITED";
    case WORKER_FAILED: return "FAILED";
    case WORKER_STOPPED: return "STOPPED";
    }
    return "UNKNOWN";
}

/* Only set flags inside signal handlers. */
static void handle_signal(int signo)
{
    if (signo == SIGCHLD) {
        child_event = 1;
    } else if (signo == SIGALRM) {
        timer_event = 1;
    }
}

static int install_handlers(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &action, NULL) == -1) return -1;
    if (sigaction(SIGALRM, &action, NULL) == -1) return -1;
    return 0;
}

static int stop_periodic_timer(void)
{
    struct itimerval disabled = {0};
    return setitimer(ITIMER_REAL, &disabled, NULL);
}

static int start_periodic_timer(void)
{
    struct itimerval timer;
    memset(&timer, 0, sizeof(timer));
    timer.it_interval.tv_sec = PERIODIC_CHECK_SECONDS;
    timer.it_value.tv_sec = PERIODIC_CHECK_SECONDS;
    return setitimer(ITIMER_REAL, &timer, NULL);
}

static bool shutdown_pending(void)
{
    return shutting_down || signal_manager_shutdown_requested() != 0;
}

static void child_fail(const char *message, size_t length)
{
    ssize_t written = write(STDERR_FILENO, message, length);
    if (written < 0) {
        _exit(127);
    }
    _exit(127);
}

static int shutdown_signal_pending(void)
{
    sigset_t pending;
    if (sigpending(&pending) == -1) return -1;
    int term = sigismember(&pending, SIGTERM);
    int intr = sigismember(&pending, SIGINT);
    if (term == -1 || intr == -1) return -1;
    return term || intr;
}

/* Block signals while creating a worker. */
static int spawn_worker(worker_t *worker)
{
    sigset_t block_set, previous_mask;
    if (sigemptyset(&block_set) == -1 ||
        sigaddset(&block_set, SIGTERM) == -1 ||
        sigaddset(&block_set, SIGINT) == -1 ||
        sigaddset(&block_set, SIGCHLD) == -1 ||
        sigprocmask(SIG_BLOCK, &block_set, &previous_mask) == -1) {
        log_msg("Worker %d could not block signals: %s", worker->id, strerror(errno));
        worker->pid = 0;
        worker->state = WORKER_FAILED;
        return -1;
    }

    int pending = shutdown_signal_pending();
    if (pending == -1 || shutdown_pending() || pending == 1) {
        int saved_errno = errno;
        if (sigprocmask(SIG_SETMASK, &previous_mask, NULL) == -1) {
            log_msg("Worker %d could not restore signal mask: %s", worker->id,
                    strerror(errno));
            return -1;
        }
        if (pending == -1) {
            log_msg("Worker %d could not check pending signals: %s", worker->id,
                    strerror(saved_errno));
            return -1;
        }
        return 1;
    }

    pid_t child = fork();
    if (child == -1) {
        int saved_errno = errno;
        if (sigprocmask(SIG_SETMASK, &previous_mask, NULL) == -1) {
            log_msg("Worker %d could not restore signal mask: %s", worker->id,
                    strerror(errno));
        }
        log_msg("Worker %d fork failed: %s", worker->id, strerror(saved_errno));
        worker->pid = 0;
        worker->state = WORKER_FAILED;
        return -1;
    }
    if (child == 0) {
        if (setpgid(0, 0) == -1) {
            static const char msg[] = "worker setpgid failed\n";
            child_fail(msg, sizeof(msg) - 1);
        }

        const int signals[] = {SIGCHLD, SIGALRM, SIGUSR1, SIGINT, SIGTERM};
        struct sigaction action = {0};
        action.sa_handler = SIG_DFL;
        if (sigemptyset(&action.sa_mask) == -1) {
            static const char msg[] = "worker sigemptyset failed\n";
            child_fail(msg, sizeof(msg) - 1);
        }
        for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); ++i) {
            if (sigaction(signals[i], &action, NULL) == -1) {
                static const char msg[] = "worker sigaction failed\n";
                child_fail(msg, sizeof(msg) - 1);
            }
        }

        sigset_t child_mask;
        if (sigemptyset(&child_mask) == -1 ||
            sigaddset(&child_mask, SIGTERM) == -1 ||
            sigprocmask(SIG_SETMASK, &child_mask, NULL) == -1) {
            static const char msg[] = "worker sigprocmask failed\n";
            child_fail(msg, sizeof(msg) - 1);
        }

        char id[16];
        (void)snprintf(id, sizeof(id), "%d", worker->id);
        execl("./worker", "worker", "--id", id, (char *)NULL);
        static const char msg[] = "worker exec failed\n";
        child_fail(msg, sizeof(msg) - 1);
    }

    worker->pid = child;
    worker->state = WORKER_RUNNING;
    if (sigprocmask(SIG_SETMASK, &previous_mask, NULL) == -1) {
        log_msg("Worker %d could not restore parent signal mask: %s", worker->id,
                strerror(errno));
        return -1;
    }
    log_msg("Worker %d started PID=%ld restart_count=%d", worker->id,
            (long)child, worker->restarts);
    return 0;
}

int process_manager_start(void)
{
    memset(workers, 0, sizeof(workers));
    for (int i = 0; i < PROCESS_MANAGER_CHILD_COUNT; ++i) {
        workers[i].id = i + 1;
        workers[i].state = WORKER_NOT_STARTED;
    }
    shutting_down = false;
    child_event = 0;
    timer_event = 0;

    if (install_handlers() == -1) {
        log_msg("Failed to install SIGCHLD/SIGALRM handlers: %s", strerror(errno));
        return -1;
    }
    for (int i = 0; i < PROCESS_MANAGER_CHILD_COUNT; ++i) {
        if (shutdown_pending()) {
            log_msg("Shutdown requested during start-up; remaining workers not started");
            break;
        }
        int result = spawn_worker(&workers[i]);
        if (result == -1) return -1;
        if (result == 1) break;
    }
    if (shutdown_pending()) return 0;
    if (start_periodic_timer() == -1) {
        log_msg("setitimer failed: %s", strerror(errno));
        return -1;
    }
    return 0;
}

static worker_t *find_worker(pid_t pid)
{
    for (int i = 0; i < PROCESS_MANAGER_CHILD_COUNT; ++i)
        if (workers[i].pid == pid) return &workers[i];
    return NULL;
}

/* Restart failed workers at most three times. */
static void apply_restart_policy(worker_t *worker, bool normal)
{
    if (shutdown_pending()) return;

    if (normal) {
        log_msg("Worker %d ended normally; no restart", worker->id);
    } else if (worker->restarts < PROCESS_MANAGER_MAX_RESTARTS) {
        worker->restarts++;
        log_msg("Worker %d unexpected termination; restart attempt=%d/%d",
                worker->id, worker->restarts, PROCESS_MANAGER_MAX_RESTARTS);
        int result = spawn_worker(worker);
        if (result == 1) {
            log_msg("Worker %d restart cancelled: shutdown requested", worker->id);
        }
    } else {
        log_msg("Worker %d restart limit reached (%d); not restarting",
                worker->id, PROCESS_MANAGER_MAX_RESTARTS);
    }
}

/* Reap exited children with waitpid. */
static void reap_children(void)
{
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        worker_t *worker = find_worker(pid);
        if (worker == NULL) {
            log_msg("Reaped unrecognized child PID=%ld", (long)pid);
            continue;
        }
        worker->pid = 0;
        bool normal = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        if (shutdown_pending())
            worker->state = WORKER_STOPPED;
        else
            worker->state = normal ? WORKER_EXITED : WORKER_FAILED;

        if (WIFEXITED(status)) {
            log_msg("Worker %d PID=%ld exited code=%d", worker->id, (long)pid,
                    WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            log_msg("Worker %d PID=%ld terminated by signal=%d", worker->id,
                    (long)pid, WTERMSIG(status));
        }
        apply_restart_policy(worker, normal);
    }
    if (pid == -1 && errno != ECHILD)
        log_msg("waitpid failed: %s", strerror(errno));
}

static void periodic_check(void)
{
    reap_children();
    for (int i = 0; i < PROCESS_MANAGER_CHILD_COUNT; ++i) {
        worker_t *worker = &workers[i];
        if (worker->state != WORKER_RUNNING || worker->pid <= 0) continue;
        if (kill(worker->pid, 0) == -1 && errno == ESRCH) {
            log_msg("Worker %d PID=%ld vanished without being reaped",
                    worker->id, (long)worker->pid);
            worker->pid = 0;
            worker->state = shutdown_pending() ? WORKER_STOPPED : WORKER_FAILED;
            apply_restart_policy(worker, false);
        }
    }
}

void process_manager_handle_events(void)
{
    if (child_event || shutting_down) {
        child_event = 0;
        reap_children();
    }
    if (timer_event) {
        timer_event = 0;
        periodic_check();
    }
}

size_t process_manager_get_status(pid_t pids[], const char *states[],
                                  size_t max_count)
{
    size_t count = 0;
    for (int i = 0; i < PROCESS_MANAGER_CHILD_COUNT && count < max_count; ++i) {
        pids[count] = workers[i].pid;
        states[count] = state_name(workers[i].state);
        count++;
    }
    return count;
}

/* Stop the timer and ask workers to exit. */
void process_manager_begin_shutdown(void)
{
    if (shutting_down) return;
    shutting_down = true;
    if (stop_periodic_timer() == -1) {
        log_msg("Could not stop periodic timer: %s", strerror(errno));
    }
    timer_event = 0;
    log_msg("Shutdown requested; sending SIGTERM to all running workers");
    for (int i = 0; i < PROCESS_MANAGER_CHILD_COUNT; ++i) {
        if (workers[i].pid > 0) {
            if (kill(workers[i].pid, SIGTERM) == -1 && errno != ESRCH)
                log_msg("SIGTERM to worker %d failed: %s", workers[i].id,
                        strerror(errno));
        }
    }
}

void process_manager_kill_remaining(void)
{
    for (int i = 0; i < PROCESS_MANAGER_CHILD_COUNT; ++i) {
        if (workers[i].pid > 0) {
            log_msg("Worker %d PID=%ld did not stop in time; sending SIGKILL",
                    workers[i].id, (long)workers[i].pid);
            if (kill(workers[i].pid, SIGKILL) == -1 && errno != ESRCH)
                log_msg("SIGKILL to worker %d failed: %s", workers[i].id,
                        strerror(errno));
        }
    }
}

int process_manager_all_stopped(void)
{
    for (int i = 0; i < PROCESS_MANAGER_CHILD_COUNT; ++i)
        if (workers[i].pid > 0) return 0;
    return 1;
}
