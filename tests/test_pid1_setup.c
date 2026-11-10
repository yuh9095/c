#define _POSIX_C_SOURCE 200809L

#include <ninit/pid1_setup.h>

#include <assert.h>
#include <fcntl.h>
#include <unistd.h>

int
main(void)
{
    int saved[3];
    int i;

    for (i = 0; i < 3; i++) {
        saved[i] = dup(i);
    }

    assert(ninit_pid1_setup_console("/dev/null") == 0);
    for (i = 0; i < 3; i++) {
        assert(fcntl(i, F_GETFD) >= 0);
    }

    for (i = 0; i < 3; i++) {
        if (saved[i] >= 0) {
            assert(dup2(saved[i], i) == i);
            close(saved[i]);
        } else {
            close(i);
        }
    }
    return 0;
}
