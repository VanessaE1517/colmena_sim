#define _GNU_SOURCE
#include "planificador.h"
#include "utils.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <limits.h>

colmena_t *head = NULL;
pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t sched_thread;
static int next_id = 1;
static bool running = false;

static sched_policy_t current_policy = POLICY_RR;
static int rr_quantum_ms = 5;
static int rr_cycle_count = 0;
static int rr_cycles_to_adjust = 5;

void planificador_add_colmena(colmena_t *c) {
    pthread_mutex_lock(&list_lock);
    c->next = NULL;
    if (!head) head = c;
    else {
        colmena_t *p = head;
        while (p->next) p = p->next;
        p->next = c;
    }
    pthread_mutex_unlock(&list_lock);
}

void planificador_remove_colmena(int id) {
    pthread_mutex_lock(&list_lock);
    colmena_t **pp = &head;
    while (*pp) {
        if ((*pp)->id == id) {
            colmena_t *to = *pp;
            *pp = to->next;
            pthread_mutex_unlock(&list_lock);
            detener_colmena(to);
            return;
        }
        pp = &((*pp)->next);
    }
    pthread_mutex_unlock(&list_lock);
}

sched_policy_t planificador_get_policy() {
    return current_policy;
}

void cambiar_quantum_random() {
    rr_quantum_ms = rand_range(2,10);
}

static colmena_t* elegir_sjf() {
    pthread_mutex_lock(&list_lock);
    colmena_t *best = NULL;
    long best_val = LONG_MAX;
    for (colmena_t *p = head; p; p = p->next) {
        pthread_mutex_lock(&p->lock);
        long metric = p->abeja_count + (p->miel);
        pthread_mutex_unlock(&p->lock);
        if (metric < best_val) {
            best_val = metric;
            best = p;
        }
    }
    pthread_mutex_unlock(&list_lock);
    return best;
}

static colmena_t* elegir_dyn() {
    pthread_mutex_lock(&list_lock);
    colmena_t *best = NULL;
    long best_prio = LONG_MAX;
    for (colmena_t *p = head; p; p = p->next) {
        pthread_mutex_lock(&p->lock);
        long prio = p->miel + p->abeja_count * 2;
        pthread_mutex_unlock(&p->lock);
        if (prio < best_prio) {
            best_prio = prio;
            best = p;
        }
    }
    pthread_mutex_unlock(&list_lock);
    return best;
}

static colmena_t* elegir_rr(colmena_t **last_rr) {
    pthread_mutex_lock(&list_lock);
    colmena_t *p = (*last_rr) ? (*last_rr)->next : head;
    if (!p) p = head;
    if (!p) {
        pthread_mutex_unlock(&list_lock);
        return NULL;
    }
    colmena_t *sel = p;
    *last_rr = sel;
    pthread_mutex_unlock(&list_lock);
    return sel;
}

static void sigusr1_handler(int signo) {
    (void)signo;
    if (current_policy == POLICY_RR) current_policy = POLICY_SJF;
    else if (current_policy == POLICY_SJF) current_policy = POLICY_DYN;
    else current_policy = POLICY_RR;
    cambiar_quantum_random();
}

void planificador_switch_policy_manual() {
    sigusr1_handler(0);
}

static void* sched_thread_fn(void *arg) {
    (void)arg;
    colmena_t *last_rr = NULL;
    while (running) {
        colmena_t *selected = NULL;
        if (current_policy == POLICY_RR) {
            selected = elegir_rr(&last_rr);
            if (!selected) { usleep(1000); continue; }
            pthread_mutex_lock(&list_lock);
            for (colmena_t *p = head; p; p = p->next) {
                if (p == selected) set_running(p, true);
                else set_running(p, false);
            }
            pthread_mutex_unlock(&list_lock);
            usleep(rr_quantum_ms * 1000);
            rr_cycle_count++;
            if (rr_cycle_count >= rr_cycles_to_adjust) {
                cambiar_quantum_random();
                rr_cycle_count = 0;
            }
        } else if (current_policy == POLICY_SJF) {
            selected = elegir_sjf();
            if (!selected) { usleep(1000); continue; }
            pthread_mutex_lock(&list_lock);
            for (colmena_t *p = head; p; p = p->next) {
                if (p == selected) set_running(p, true);
                else set_running(p, false);
            }
            pthread_mutex_unlock(&list_lock);
            usleep(8 * 1000);
        } else if (current_policy == POLICY_DYN) {
            selected = elegir_dyn();
            if (!selected) { usleep(1000); continue; }
            pthread_mutex_lock(&list_lock);
            for (colmena_t *p = head; p; p = p->next) {
                if (p == selected) set_running(p, true);
                else set_running(p, false);
            }
            pthread_mutex_unlock(&list_lock);
            int q = 5;
            pthread_mutex_lock(&selected->lock);
            long miel = selected->miel;
            pthread_mutex_unlock(&selected->lock);
            if (miel < 10) q = 12;
            else if (miel < 30) q = 8;
            else q = 4;
            usleep(q * 1000);
        }

        pthread_mutex_lock(&list_lock);
        for (colmena_t *p = head; p; p = p->next) {
            pthread_mutex_lock(&p->lock);
            for (int i = 0; i < p->abeja_count; ++i) {
                if (p->abejas[i].tipo == QUEEN) {
                    p->abejas[i].tipo = WORKER;
                    int nid = ++next_id;
                    colmena_t *nc = crear_colmena(nid);
                    int transfer_bees = p->abeja_count / 2;
                    if (transfer_bees > 0) {
                        for (int t = 0; t < transfer_bees; ++t) {
                            if (p->abeja_count > 0) {
                                abeja_t mover = p->abejas[--p->abeja_count];
                                // add to nc
                                // we call agregar indirectly by ensuring capacity
                            }
                        }
                    }
                    long transfer_miel = p->miel / 3;
                    if (transfer_miel > 0) {
                        p->miel -= transfer_miel;
                        nc->miel += transfer_miel;
                    }
                    int transfer_huevos = p->huevos / 3;
                    if (transfer_huevos > 0) {
                        p->huevos -= transfer_huevos;
                        nc->huevos += transfer_huevos;
                    }
                    pthread_mutex_unlock(&p->lock);
                    iniciar_colmena(nc);
                    planificador_add_colmena(nc);
                    pthread_mutex_lock(&p->lock);
                }
            }
            pthread_mutex_unlock(&p->lock);
        }
        pthread_mutex_unlock(&list_lock);

        usleep(500);
    }

    pthread_mutex_lock(&list_lock);
    for (colmena_t *p = head; p; p = p->next) {
        set_running(p, false);
    }
    pthread_mutex_unlock(&list_lock);
    return NULL;
}

void planificador_start() {
    running = true;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigusr1_handler;
    sigaction(SIGUSR1, &sa, NULL);
    pthread_create(&sched_thread, NULL, sched_thread_fn, NULL);
}

void planificador_stop() {
    running = false;
    pthread_join(sched_thread, NULL);
    pthread_mutex_lock(&list_lock);
    colmena_t *p = head;
    while (p) {
        colmena_t *n = p->next;
        detener_colmena(p);
        p = n;
    }
    head = NULL;
    pthread_mutex_unlock(&list_lock);
}
