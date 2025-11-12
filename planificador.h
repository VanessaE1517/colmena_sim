#ifndef PLANIFICADOR_H
#define PLANIFICADOR_H

#include "colmena.h"

typedef enum { POLICY_RR, POLICY_SJF, POLICY_DYN } sched_policy_t;

void planificador_init();
void planificador_add_colmena(colmena_t *c);
void planificador_remove_colmena(int id);
void planificador_start();
void planificador_stop();

sched_policy_t planificador_get_policy();
void planificador_switch_policy_manual();

extern colmena_t *head;
extern pthread_mutex_t list_lock;

#endif // PLANIFICADOR_H

