#ifndef NINIT_LOG_H
#define NINIT_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

enum ninit_log_level {
    NINIT_LOG_EMERG = 0,
    NINIT_LOG_ALERT,
    NINIT_LOG_CRIT,
    NINIT_LOG_ERR,
    NINIT_LOG_WARNING,
    NINIT_LOG_NOTICE,
    NINIT_LOG_INFO,
    NINIT_LOG_DEBUG
};

/* Return the stable textual name for a log level, or "UNKNOWN". */
const char *ninit_log_level_name(enum ninit_log_level level);

#ifdef __cplusplus
}
#endif

#endif
