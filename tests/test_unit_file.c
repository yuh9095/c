#define _POSIX_C_SOURCE 200809L

#include <ninit/unit_file.h>

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
main(void)
{
    char directory[] = "/tmp/ninit-unit-file-XXXXXX";
    char service_path[256];
    char target_path[256];
    struct ninit_unit_manager *manager;
    const struct ninit_unit *unit;
    FILE *file;

    assert(mkdtemp(directory) != NULL);
    assert(snprintf(service_path, sizeof(service_path),
                    "%s/demo.service", directory) > 0);
    assert(snprintf(target_path, sizeof(target_path),
                    "%s/base.target", directory) > 0);
    manager = ninit_unit_manager_create();
    assert(manager != NULL);

    file = fopen(service_path, "w");
    assert(file != NULL);
    fputs("[Unit]\n"
          "Description=Demo service\n"
          "Requires=base.target\n"
          "Wants=optional.service\n"
          "After=base.target\n"
          "[Service]\n"
          "ExecStart=/bin/echo \"hello world\" escaped\\ value\n",
          file);
    assert(fclose(file) == 0);

    file = fopen(target_path, "w");
    assert(file != NULL);
    fputs("[Unit]\nDescription=Base target\n[Target]\n", file);
    assert(fclose(file) == 0);

    assert(ninit_unit_file_load(manager, target_path) == 0);
    assert(ninit_unit_file_load(manager, service_path) == 0);
    unit = ninit_unit_manager_find(manager, "demo.service");
    assert(unit != NULL);
    assert(unit->type == NINIT_UNIT_SERVICE);
    assert(strcmp(unit->description, "Demo service") == 0);
    assert(unit->requires_count == 1);
    assert(strcmp(unit->requires[0], "base.target") == 0);
    assert(unit->wants_count == 1);
    assert(strcmp(unit->wants[0], "optional.service") == 0);
    assert(unit->after_count == 1);
    assert(strcmp(unit->exec_start_argv[0], "/bin/echo") == 0);
    assert(strcmp(unit->exec_start_argv[1], "hello world") == 0);
    assert(strcmp(unit->exec_start_argv[2], "escaped value") == 0);
    assert(unit->exec_start_argv[3] == NULL);

    ninit_unit_manager_destroy(manager);
    assert(unlink(service_path) == 0);
    assert(unlink(target_path) == 0);
    assert(rmdir(directory) == 0);
    return 0;
}
