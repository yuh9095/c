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

static void
record_child_exit(pid_t pid, int status, void *userdata)
{
    struct reap_result *result = userdata;

    assert(pid == result->expected_pid);
    result->called++;
    result->status = status;
}

int
main(void)
{
    struct reap_result result = { -1, 0, 0 };
    pid_t child;
    int reaped;

    /* The test process is not PID 1 in the usual test environment. */
    if (getpid() != 1) {
        assert(ninit_pid1_is_init() == 0);
    }

    child = fork();
    assert(child >= 0);
    if (child == 0) {
        _exit(37);
    }

    result.expected_pid = child;
    do {
        struct timespec pause = { 0, 1000000L };
        reaped = ninit_pid1_reap_children(record_child_exit, &result);
        if (reaped == 0) {
            (void) nanosleep(&pause, NULL);
        }
    } while (reaped == 0);

    assert(reaped == 1);
    assert(result.called == 1);
    assert(WIFEXITED(result.status));
    assert(WEXITSTATUS(result.status) == 37);

    /* Reaping again with no children is a successful empty result. */
    assert(ninit_pid1_reap_children(NULL, NULL) == 0);
    return 0;
}
