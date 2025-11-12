
#include "utils.h"
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>

long now_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long)(tv.tv_sec * 1000L + tv.tv_usec / 1000L);
}

int rand_range(int a, int b) {
    if (b < a) return a;
    return a + rand() % (b - a + 1);
}

void mkdir_if_needed(const char *path) {
    struct stat st = {0};
    if (stat(path, &st) == -1) {
        if (mkdir(path, 0755) != 0) {
            if (errno != EEXIST) {
                perror("mkdir");
            }
        }
    }
}
