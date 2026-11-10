#define _POSIX_C_SOURCE 200809L

#include <ninit/pid1_runtime.h>

#include <assert.h>
#include <signal.h>
#include <stddef.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

struct reap_result {
    pid_t expected_pid;
    int called;
    int status;
};

struct signal_result {
    int child_seen;
};

static void
record_child_exit(pid_t pid, int status, void *userdata)
{
    struct reap_result *result = userdata;

    assert(pid == result->expected_pid);
    result->called++;
    result->status = status;
}

static void
record_signal(int signal_number, void *userdata)
{
    struct signal_result *result = userdata;

    if (signal_number == SIGCHLD) {
        result->child_seen++;
    }
}

int
main(void)
{
    struct reap_result result = { -1, 0, 0 };
    struct ninit_pid1_event_source events;
    struct signal_result signals = { 0 };
    pid_t child;
    int event_count;

    if (getpid() != 1) {
        assert(ninit_pid1_is_init() == 0);
    }

    assert(ninit_pid1_event_source_init(&events) == 0);
    assert(ninit_pid1_event_source_fd(&events) >= 0);

    child = fork();
    assert(child >= 0);
    if (child == 0) {
        _exit(37);
    }

    result.expected_pid = child;
    event_count = ninit_pid1_event_source_wait(
        &events, 3000, record_signal, &signals,
        record_child_exit, &result);
    assert(event_count >= 2);
    assert(signals.child_seen >= 1);
    assert(result.called == 1);
    assert(result.called == 1);
    assert(WIFEXITED(result.status));
    assert(WEXITSTATUS(result.status) == 37);

    ninit_pid1_event_source_destroy(&events);
    return 0;
}
