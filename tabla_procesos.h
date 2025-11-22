#ifndef TABLA_PROCESOS_H
#define TABLA_PROCESOS_H

#include "colmena.h"

typedef struct tabla_global {
    long total_exec_ms;
    long total_io_wait_ms;
    long total_ready_wait_ms;

    long avg_exec_ms;
    long avg_io_wait_ms;
    long avg_ready_wait_ms;

    long total_bees;
    long total_honey;
    long total_eggs;

    int num_colmenas;
} tabla_global_t;

void tabla_global_actualizar();
tabla_global_t tabla_global_obtener();

#endif
