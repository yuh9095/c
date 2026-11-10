#include <ninit/log.h>

const char *
ninit_log_level_name(enum ninit_log_level level)
{
    static const char *const names[] = {
        "EMERG",
        "ALERT",
        "CRIT",
        "ERR",
        "WARNING",
        "NOTICE",
        "INFO",
        "DEBUG"
    };

    if ((unsigned int) level >=
        (unsigned int) (sizeof(names) / sizeof(names[0]))) {
        return "UNKNOWN";
    }

    return names[level];
}
