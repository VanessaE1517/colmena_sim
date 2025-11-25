#ifndef PLANIFICADOR_H
#define PLANIFICADOR_H

#include "colmena.h"

typedef enum
{
    POLICY_RR,
    POLICY_SJF,
    POLICY_DYN
} sched_policy_t;

void planificador_init();
void planificador_add_colmena(colmena_t *c);
void planificador_remove_colmena(int id);
void planificador_start();
void planificador_stop();

sched_policy_t planificador_get_policy();
void planificador_switch_policy_manual();

extern colmena_t *head;
extern pthread_mutex_t list_lock;

// ESTADÍSTICAS GLOBALES
typedef struct
{
    long total_exec_ms;
    long total_ready_wait;
    long total_io_wait;

    long avg_exec_ms;
    long avg_ready_wait;
    long avg_io_wait;

    long total_miel;
    long total_huevos;
    long total_abejas;

    int colmenas_activas;

    long sched_cycles;
    long context_switches;
    int last_selected_id;
    int quantum_actual;

} global_stats_t;

extern global_stats_t gstats;

#endif