#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s <seconds>\n", prog);
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc != 2) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    errno = 0;
    char *endptr = NULL;
    long seconds = strtol(argv[1], &endptr, 10);
    if (errno != 0 || endptr == argv[1] || *endptr != '\0' || seconds <= 0) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    printf("[demo] Starting %ld-second job...\n", seconds);
    for (long i = 1; i <= seconds; i++) {
        printf("[demo] Iteration %ld/%ld\n", i, seconds);
        sleep(1);
    }
    printf("[demo] Completed %ld iterations.\n", seconds);
    return EXIT_SUCCESS;
}
