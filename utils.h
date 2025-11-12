#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>
#include <sys/time.h>

long now_ms(); // tiempo actual en ms
int rand_range(int a, int b); // inclusivo
void mkdir_if_needed(const char *path);

#endif // UTILS_H
