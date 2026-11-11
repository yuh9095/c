#ifndef NINIT_UNIT_H
#define NINIT_UNIT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum ninit_unit_type {
    NINIT_UNIT_SERVICE = 1,
    NINIT_UNIT_TARGET
};

/*
 * Unit specifications are borrowed input. ninit_unit_manager_add() deep-copies
 * every string and dependency name; callers may release their input afterwards.
 */
struct ninit_unit_spec {
    const char *name;
    const char *description;
    enum ninit_unit_type type;
    const char *const *exec_start_argv;
    size_t exec_start_argc;
    const char *const *requires;
    size_t requires_count;
    const char *const *wants;
    size_t wants_count;
    const char *const *after;
    size_t after_count;
    const char *const *before;
    size_t before_count;
    const char *const *exec_stop_argv;
    size_t exec_stop_argc;
    int oneshot;
    int keyword_no_shutdown;
    int keyword_no_stop;
    unsigned int timeout_stop_sec;
};

/* Unit objects are owned by the manager and remain valid until removal/destruction. */
struct ninit_unit {
    char *name;
    char *description;
    enum ninit_unit_type type;
    char **exec_start_argv;
    size_t exec_start_argc;
    char **requires;
    size_t requires_count;
    char **wants;
    size_t wants_count;
    char **after;
    size_t after_count;
    char **before;
    size_t before_count;
    char **exec_stop_argv;
    size_t exec_stop_argc;
    int oneshot;
    int keyword_no_shutdown;
    int keyword_no_stop;
    unsigned int timeout_stop_sec;
};

#ifdef __cplusplus
}
#endif

#endif
