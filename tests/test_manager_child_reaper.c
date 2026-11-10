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
};

static void
record_exit(pid_t pid, int status, void *userdata)
{
    struct result *result = userdata;
    assert(pid == result->expected);
    result->called++;
    result->status = status;
}

int
main(void)
{
    struct ninit_manager_child_reaper *reaper;
    struct pollfd descriptor;
    struct result result = { -1, 0, 0 };
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

    ninit_manager_child_reaper_destroy(reaper);
    return 0;
}
