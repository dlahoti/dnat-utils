#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

#include "inotify.h"
#include "nat_table.h"
#include "nfqueue.h"

void *nfq_loop_wrapper(void *queue_num) {
    nfq_loop(*((unsigned int *) queue_num));
    return NULL;
}

int main(int argc, char **argv) {
    unsigned int queue_num;
    pthread_t nfq_thread;
    char *endptr = NULL;

    if (argc != 3) {
        goto usage;
    }

    endptr = NULL;
    queue_num = (unsigned int) strtoul(argv[1], &endptr, 10);
    if (argv[1][0] == '\0' || *endptr != '\0') {
        goto usage;
    }

    nt_read(argv[2]);

    pthread_create(&nfq_thread, NULL, nfq_loop_wrapper, &queue_num);

    in_watch(argv[2]);

    exit(EXIT_SUCCESS);

usage:
    fprintf(stderr, "usage: %s queue_num /path/to/csv\n", argv[0]);
    exit(EXIT_FAILURE);
}
