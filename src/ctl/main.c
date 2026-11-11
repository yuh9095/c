#include <stdio.h>
#include <string.h>

#include <ninit/version.h>

static void
print_usage(const char *program)
{
    printf("Usage: %s [--version|--help]\n", program);
    puts("Ninit control client (command interface not implemented yet).");
}

int
main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("ninitctl %s\n", NINIT_VERSION_STRING);
        return 0;
    }

    if (argc == 1 || (argc == 2 && strcmp(argv[1], "--help") == 0)) {
        print_usage(argv[0]);
        return 0;
    }

    fprintf(stderr, "ninitctl: command interface is not implemented yet\n");
    return 2;
}
