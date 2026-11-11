#define _POSIX_C_SOURCE 200809L

#include <ninit/pid1_runtime.h>

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <sys/prctl.h>
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

static void
test_reap_exit_status(void)
{
    struct reap_result result = { -1, 0, 0 };
    pid_t child;
    int reaped;

    child = fork();
    assert(child >= 0);
    if (child == 0)
        _exit(37);

    result.expected_pid = child;
    do {
        struct timespec pause_time = { 0, 1000000L };
        reaped = ninit_pid1_reap_children(record_child_exit, &result);
        if (reaped == 0)
            (void) nanosleep(&pause_time, NULL);
    } while (reaped == 0);

    assert(reaped == 1);
    assert(result.called == 1);
    assert(WIFEXITED(result.status));
    assert(WEXITSTATUS(result.status) == 37);
    assert(ninit_pid1_reap_children(NULL, NULL) == 0);
}

static void
test_cleanup_after_manager_crash(void)
{
    int ready_pipe[2];
    int previous_subreaper = 0;
    int manager_status;
    int service_status;
    pid_t manager;
    pid_t service;
    pid_t reaped;

    assert(prctl(PR_GET_CHILD_SUBREAPER, &previous_subreaper) == 0);
    assert(prctl(PR_SET_CHILD_SUBREAPER, 1) == 0);
    assert(pipe(ready_pipe) == 0);

    manager = fork();
    assert(manager >= 0);
    if (manager == 0) {
        pid_t service_child;
        close(ready_pipe[0]);
        service_child = fork();
        if (service_child < 0)
            _exit(90);
        if (service_child == 0) {
            pid_t background_child;
            struct orphan_tree_pids ready;
            (void) setpgid(0, 0);
            background_child = fork();
            if (background_child < 0)
                _exit(92);
            if (background_child == 0) {
                (void) signal(SIGTERM, SIG_IGN);
                ready.service = getppid();
                ready.background = getpid();
                if (write(ready_pipe[1], &ready, sizeof(ready)) !=
                    (ssize_t) sizeof(ready))
                    _exit(93);
                close(ready_pipe[1]);
                for (;;)
                    pause();
            }
            close(ready_pipe[1]);
            for (;;)
                pause();
        }
        close(ready_pipe[1]);
        for (;;)
            pause();
    }

    close(ready_pipe[1]);
    assert(read(ready_pipe[0], &tree, sizeof(tree)) ==
           (ssize_t) sizeof(tree));
    close(ready_pipe[0]);
    assert(tree.service > 1);
    assert(tree.background > 1);
    assert(kill(manager, SIGKILL) == 0);
    assert(waitpid(manager, &manager_status, 0) == manager);
    assert(WIFSIGNALED(manager_status));
    assert(WTERMSIG(manager_status) == SIGKILL);

    /*
     * The test process acts as a subreaper to model PID 1: killing Manager
     * reparents its service and background descendant to this process.
     */
    assert(ninit_pid1_cleanup_orphaned_children(100) == 0);
    errno = 0;
    reaped = waitpid(tree.service, &service_status, WNOHANG);
    assert(reaped == -1);
    assert(errno == ECHILD);
    errno = 0;
    reaped = waitpid(tree.background, &service_status, WNOHANG);
    assert(reaped == -1);
    assert(errno == ECHILD);
    assert(ninit_pid1_reap_children(NULL, NULL) == 0);

    assert(prctl(PR_SET_CHILD_SUBREAPER, previous_subreaper) == 0);
}

int
main(void)
{
    /* The test process is not PID 1 in the usual test environment. */
    if (getpid() != 1)
        assert(ninit_pid1_is_init() == 0);

    test_reap_exit_status();
    test_cleanup_after_manager_crash();
    return 0;
}
