#include <assert.h>
#include <string.h>

#include <ninit/log.h>

int
main(void)
{
    assert(strcmp(ninit_log_level_name(NINIT_LOG_EMERG), "EMERG") == 0);
    assert(strcmp(ninit_log_level_name(NINIT_LOG_ERR), "ERR") == 0);
    assert(strcmp(ninit_log_level_name(NINIT_LOG_DEBUG), "DEBUG") == 0);
    assert(strcmp(ninit_log_level_name((enum ninit_log_level) 99),
                  "UNKNOWN") == 0);
    return 0;
}
