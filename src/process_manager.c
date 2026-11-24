#define _POSIX_C_SOURCE 200809L
#include "process_manager.h"
#include "logger.h"

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

/* Set by the signal handler only (async-signal-safe); used by the main flow. */
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

/* ---- signal handling: flags only, no logging / allocation / fork ---- */

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

static void stop_periodic_timer(void)
{
    struct itimerval disabled;
    memset(&disabled, 0, sizeof(disabled));   /* zero it_value disarms the timer */
    (void)setitimer(ITIMER_REAL, &disabled, NULL);
}

static int start_periodic_timer(void)
{
    struct itimerval timer;
    memset(&timer, 0, sizeof(timer));
    timer.it_interval.tv_sec = PERIODIC_CHECK_SECONDS;
    timer.it_value.tv_sec = PERIODIC_CHECK_SECONDS;
    return setitimer(ITIMER_REAL, &timer, NULL);
}

/* ---- worker creation ---- */

/* Child-side fatal error: write a short message (async-signal-safe) and leave. */
static void child_fail(const char *message, size_t length)
{
    ssize_t written = write(STDERR_FILENO, message, length);
    (void)written;
    _exit(127);
}

static int spawn_worker(worker_t *worker)
{
    /* Block SIGTERM BEFORE fork(). The child is then created with SIGTERM
       already blocked, so there is no window in which a shutdown SIGTERM could
       reach it before it is ready (it would hit the inherited watchdog handler
       and be lost, or kill the worker). In the parent the signal simply stays
       pending until we restore the mask right after fork(). */
    sigset_t block_term, previous_mask;
    sigemptyset(&block_term);
    sigaddset(&block_term, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &block_term, &previous_mask) == -1) {
        log_msg("Worker %d sigprocmask failed: %s", worker->id, strerror(errno));
        worker->pid = 0;
        worker->state = WORKER_FAILED;
        return -1;
    }

    pid_t child = fork();
    if (child == -1) {
        int saved_errno = errno;
        (void)sigprocmask(SIG_SETMASK, &previous_mask, NULL);
        log_msg("Worker %d fork failed: %s", worker->id, strerror(saved_errno));
        worker->pid = 0;
        worker->state = WORKER_FAILED;
        return -1;
    }
    if (child == 0) {
        /* Own process group: a terminal Ctrl+C (SIGINT to the whole foreground
           group) must reach only the watchdog, which then shuts workers down
           with SIGTERM -> SIGKILL. Without it the worker would die by SIGINT,
           so a failure here is fatal for this worker. */
        if (setpgid(0, 0) == -1) {
            static const char msg[] = "worker setpgid failed\n";
            child_fail(msg, sizeof(msg) - 1);
        }

        /* Keep ONLY SIGTERM blocked (it already is, inherited from the parent)
           and let it survive exec(): the worker unblocks it right after
           installing its own handler. */
        if (sigprocmask(SIG_SETMASK, &block_term, NULL) == -1) {
            static const char msg[] = "worker sigprocmask failed\n";
            child_fail(msg, sizeof(msg) - 1);
        }

        /* Workers must not run the parent's handlers. */
        const int signals[] = {SIGCHLD, SIGALRM, SIGUSR1, SIGINT, SIGTERM};
        struct sigaction action;
        memset(&action, 0, sizeof(action));
        action.sa_handler = SIG_DFL;
        sigemptyset(&action.sa_mask);
        for (unsigned i = 0; i < sizeof(signals) / sizeof(signals[0]); ++i)
            (void)sigaction(signals[i], &action, NULL);

        char id[16];
        (void)snprintf(id, sizeof(id), "%d", worker->id);
        execl("./worker", "worker", "--id", id, (char *)NULL);

        /* Reached only if execl failed. */
        static const char msg[] = "worker exec failed\n";
        child_fail(msg, sizeof(msg) - 1);
    }

    /* Parent: restore the original mask; a SIGTERM that arrived meanwhile is
       delivered to the watchdog's own handler now. */
    (void)sigprocmask(SIG_SETMASK, &previous_mask, NULL);

    worker->pid = child;
    worker->state = WORKER_RUNNING;
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

    /* Handlers first, so no SIGCHLD can be missed after the first fork. */
    if (install_handlers() == -1) {
        log_msg("Failed to install SIGCHLD/SIGALRM handlers: %s", strerror(errno));
        return -1;
    }
    for (int i = 0; i < PROCESS_MANAGER_CHILD_COUNT; ++i) {
        if (spawn_worker(&workers[i]) == -1) return -1;
    }
    if (start_periodic_timer() == -1) {
        log_msg("setitimer failed: %s", strerror(errno));
        return -1;
    }
    return 0;
}

/* ---- termination monitoring and restart policy ---- */

static worker_t *find_worker(pid_t pid)
{
    for (int i = 0; i < PROCESS_MANAGER_CHILD_COUNT; ++i)
        if (workers[i].pid == pid) return &workers[i];
    return NULL;
}

static void apply_restart_policy(worker_t *worker, bool normal)
{
    if (shutting_down) return;   /* no new processes during shutdown */

    if (normal) {
        log_msg("Worker %d ended normally; no restart", worker->id);
    } else if (worker->restarts < PROCESS_MANAGER_MAX_RESTARTS) {
        /* The counter is incremented BEFORE the restart attempt. */
        worker->restarts++;
        log_msg("Worker %d unexpected termination; restart attempt=%d/%d",
                worker->id, worker->restarts, PROCESS_MANAGER_MAX_RESTARTS);
        (void)spawn_worker(worker);
    } else {
        log_msg("Worker %d restart limit reached (%d); not restarting",
                worker->id, PROCESS_MANAGER_MAX_RESTARTS);
    }
}

/* Reaps every terminated child. SIGCHLD can coalesce, so loop until none left. */
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
        if (shutting_down)
            worker->state = WORKER_STOPPED;   /* stopped by our own shutdown */
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

/* Periodic check driven by the SIGALRM/setitimer tick (once per second):
   safety-net reaping plus a liveness check of every worker we think is alive. */
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
            worker->state = shutting_down ? WORKER_STOPPED : WORKER_FAILED;
            apply_restart_policy(worker, false);
        }
    }
}

void process_manager_handle_events(void)
{
    /* During shutdown the periodic timer is stopped, so always reap here. */
    if (child_event || shutting_down) {
        child_event = 0;       /* clear BEFORE reaping: a new SIGCHLD re-sets it */
        reap_children();
    }
    if (timer_event) {
        timer_event = 0;
        periodic_check();
    }
}

/* ---- status, shutdown ---- */

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

void process_manager_begin_shutdown(void)
{
    if (shutting_down) return;
    shutting_down = true;
    stop_periodic_timer();   /* no periodic checks needed while shutting down */
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
