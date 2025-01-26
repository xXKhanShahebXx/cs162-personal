#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>

int main() {
    struct rlimit lim;
    if (getrlimit(RLIMIT_STACK, &lim) == -1) {
        perror("getrlimit(RLIMIT_STACK)");
        return EXIT_FAILURE;
    }
    printf("stack size: %ld\n", (long) lim.rlim_cur);

    if (getrlimit(RLIMIT_NPROC, &lim) == -1) {
        perror("getrlimit(RLIMIT_NPROC)");
        return EXIT_FAILURE;
    }
    printf("process limit: %ld\n", (long) lim.rlim_cur);

    if (getrlimit(RLIMIT_NOFILE, &lim) == -1) {
        perror("getrlimit(RLIMIT_NOFILE)");
        return EXIT_FAILURE;
    }
    printf("max file descriptors: %ld\n", (long) lim.rlim_cur);

    return 0;
}
