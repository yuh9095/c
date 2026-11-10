#ifndef NINIT_UNIT_FILE_H
#define NINIT_UNIT_FILE_H

#include <ninit/unit_manager.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Load one native Unit file. The basename must end in .service or .target.
 * Supported sections are [Unit], [Service], and [Target].
 * ExecStart is tokenized without invoking a shell or expanding variables.
 */
int ninit_unit_file_load(struct ninit_unit_manager *manager, const char *path);

#ifdef __cplusplus
}
#endif

#endif
