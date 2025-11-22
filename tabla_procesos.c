#include "tabla_procesos.h"
#include "planificador.h"
#include <pthread.h>
#include "planificador.h"


static tabla_global_t tg;

void tabla_global_actualizar() {
    pthread_mutex_lock(&list_lock);

    tg.total_exec_ms = 0;
    tg.total_io_wait_ms = 0;
    tg.total_ready_wait_ms = 0;

    tg.total_bees = 0;
    tg.total_honey = 0;
    tg.total_eggs = 0;

    tg.num_colmenas = 0;

    colmena_t *p = head;

    while (p) {
        pthread_mutex_lock(&p->lock);

        tg.total_exec_ms     += p->pcb.total_exec_ms;
        tg.total_io_wait_ms  += p->pcb.io_wait_ms;
        tg.total_ready_wait_ms += p->pcb.ready_wait_ms;

        tg.total_bees  += p->abeja_count;
        tg.total_honey += p->miel;
        tg.total_eggs  += p->huevos;

        tg.num_colmenas++;

        pthread_mutex_unlock(&p->lock);
        p = p->next;
    }

    if (tg.num_colmenas > 0) {
        tg.avg_exec_ms  = tg.total_exec_ms  / tg.num_colmenas;
        tg.avg_io_wait_ms  = tg.total_io_wait_ms / tg.num_colmenas;
        tg.avg_ready_wait_ms = tg.total_ready_wait_ms / tg.num_colmenas;
    }

    pthread_mutex_unlock(&list_lock);
}

tabla_global_t tabla_global_obtener() {
    return tg;
}

