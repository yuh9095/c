#include <stdio.h>
#include <string.h>

#include <ninit/version.h>

static void
print_usage(const char *program)
{
    printf("Usage: %s [--version|--help]\n", program);
    puts("Ninit service manager (development stub).");
}

int
main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("ninit-manager %s\n", NINIT_VERSION_STRING);
        return 0;
    }

    if (argc == 1 || (argc == 2 && strcmp(argv[1], "--help") == 0)) {
        print_usage(argv[0]);
        return 0;
    }

    fprintf(stderr, "ninit-manager: unsupported argument\n");
    print_usage(argv[0]);
    return 2;
}
