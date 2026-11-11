#define _POSIX_C_SOURCE 200809L

#include <ninit/manager_child_reaper.h>

#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

struct result {
    pid_t expected;
    int called;
    int status;
    pid_t adopted_pid;
    int adopted_status;
};

static void
record_exit(pid_t pid, int status, void *userdata)
{
    struct result *result = userdata;
    if (pid == result->expected) {
        result->called++;
        result->status = status;
    } else {
        result->adopted_pid = pid;
        result->adopted_status = status;
    }
}

int
main(void)
{
    struct ninit_manager_child_reaper *reaper;
    struct pollfd descriptor;
    struct result result = { -1, 0, 0, -1, 0 };
    pid_t child;
    int poll_result;
    int count;

    reaper = ninit_manager_child_reaper_create();
    assert(reaper != NULL);
    descriptor.fd = ninit_manager_child_reaper_fd(reaper);
    assert(descriptor.fd >= 0);
    descriptor.events = POLLIN;
    descriptor.revents = 0;

    child = fork();
    assert(child >= 0);
    if (child == 0)
        _exit(43);

    result.expected = child;
    do {
        poll_result = poll(&descriptor, 1, 3000);
    } while (poll_result < 0 && errno == EINTR);
    assert(poll_result == 1);
    assert((descriptor.revents & POLLIN) != 0);

    count = ninit_manager_child_reaper_drain(reaper, record_exit, &result);
    assert(count == 1);
    assert(result.called == 1);
    assert(WIFEXITED(result.status));
    assert(WEXITSTATUS(result.status) == 43);
    assert(ninit_manager_child_reaper_drain(reaper, NULL, NULL) == 0);

    /*
     * A grandchild orphaned by its intermediate parent must be adopted by
     * this manager process and delivered through the same reaper callback.
     */
    {
        pid_t manager_pid = getpid();
        pid_t intermediate = fork();
        assert(intermediate >= 0);
        if (intermediate == 0) {
            pid_t intermediate_pid = getpid();
            pid_t grandchild = fork();
            if (grandchild < 0)
                _exit(90);
            if (grandchild == 0) {
                struct timespec delay = { 0, 1000000L };
                int attempts;
                for (attempts = 0; attempts < 3000; ++attempts) {
                    pid_t parent = getppid();
                    if (parent != intermediate_pid) {
                        _exit(parent == manager_pid ? 44 : 99);
                    }
                    (void)nanosleep(&delay, NULL);
                }
                _exit(98);
            }
            _exit(0);
        }
        result.expected = intermediate;
        result.called = 0;
        result.adopted_pid = -1;
        for (;;) {
            int drained;
            descriptor.revents = 0;
            do {
                poll_result = poll(&descriptor, 1, 3000);
            } while (poll_result < 0 && errno == EINTR);
            assert(poll_result == 1);
            drained = ninit_manager_child_reaper_drain(
                reaper, record_exit, &result);
            assert(drained >= 1);
            if (result.called == 1 && result.adopted_pid > 0)
                break;
        }
        assert(WIFEXITED(result.status) && WEXITSTATUS(result.status) == 0);
        assert(WIFEXITED(result.adopted_status));
        assert(WEXITSTATUS(result.adopted_status) == 44);
        assert(ninit_manager_child_reaper_drain(reaper, NULL, NULL) == 0);
    }

    /*
     * Final manager cleanup can enumerate adopted children and signal their
     * process groups, not merely the direct child PID.
     */
    {
        pid_t survivor = fork();
        assert(survivor >= 0);
        if (survivor == 0) {
            if (setpgid(0, 0) < 0)
                _exit(91);
            for (;;)
                pause();
        }
        result.expected = survivor;
        result.called = 0;
        assert(ninit_manager_child_reaper_has_children(reaper) == 1);
        assert(ninit_manager_child_reaper_signal_children(
                   reaper, SIGTERM) >= 1);
        descriptor.revents = 0;
        do {
            poll_result = poll(&descriptor, 1, 3000);
        } while (poll_result < 0 && errno == EINTR);
        assert(poll_result == 1);
        assert(ninit_manager_child_reaper_drain(
                   reaper, record_exit, &result) == 1);
        assert(result.called == 1);
        assert(WIFSIGNALED(result.status));
        assert(WTERMSIG(result.status) == SIGTERM);
        assert(ninit_manager_child_reaper_has_children(reaper) == 0);
    }

    ninit_manager_child_reaper_destroy(reaper);
    return 0;
}
