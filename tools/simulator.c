#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include <string.h>
#include "../bloom_filter.h"

int main(int argc, char *argv[])
{
    char *key = "0";
    blooms_init(strtoull(argv[1], 0, 10));

    blooms_add(key, strtoull(argv[2], 0, 10), strtod(argv[3], 0));
    bloom_status(key);

    unsigned int conflicted = 0;
    unsigned int total_uid = 0;
    char buf[BUFSIZ];

    while (fgets(buf, BUFSIZ, stdin)) {
        int len = strlen(buf);
        while (buf[len - 1] == '\n') buf[--len] = '\0';
        while (buf[len - 1] == '\r') buf[--len] = '\0';

        total_uid ++;

        if (blooms_get(key, buf)) {
            conflicted ++;
            continue;
        }

        blooms_set(key, buf);
    }

    bloom_status(key);
    blooms_delete(key);

    fprintf(stderr, "E.M.A.O.E: %s, F.P.R: %s, total: %u, conflicted: %u, rate: %f\r\n", 
        argv[2], argv[3], total_uid, conflicted, (float)conflicted/total_uid);

    return 0;
}

