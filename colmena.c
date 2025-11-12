#define _GNU_SOURCE
#include "colmena.h"
#include "utils.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

static void asegurar_capacity(colmena_t *c, int extra) {
    if (c->abeja_count + extra > c->abeja_capacity) {
        int newcap = (c->abeja_capacity == 0) ? 64 : c->abeja_capacity * 2;
        while (newcap < c->abeja_count + extra) newcap *= 2;
        abeja_t *n = realloc(c->abejas, sizeof(abeja_t) * newcap);
        if (!n) {
            perror("realloc abeja");
            exit(1);
        }
        c->abejas = n;
        c->abeja_capacity = newcap;
    }
}

static void agregar_abeja(colmena_t *c, abeja_tipo t) {
    asegurar_capacity(c, 1);
    abeja_t *a = &c->abejas[c->abeja_count++];
    a->tipo = t;
    a->pollen_collected = 0;
    a->alive = true;
}

static void hatch_eggs(colmena_t *c) {
    if (c->huevos > 0) {
        int hatch = rand_range(0, c->huevos);
        if (hatch > 0) {
            for (int i = 0; i < hatch; ++i) {
                int r = rand_range(1, 1000);
                if (r <= 5) {
                    agregar_abeja(c, QUEEN);
                } else {
                    agregar_abeja(c, WORKER);
                }
            }
            c->huevos -= hatch;
        }
    }
}

static void producir_miel_desde_polen(colmena_t *c) {
    if (c->polen_acumulado >= 10) {
        long miel_nueva = c->polen_acumulado / 10;
        c->miel += miel_nueva;
        c->polen_acumulado -= miel_nueva * 10;
    }
}

static void escribir_log_colmena_interno(colmena_t *c) {
    FILE *f = fopen(c->logfile, "a");
    if (!f) return;
    long t = now_ms();
    fprintf(f, "ts=%ld id=%d abejas=%d huevos=%d polen=%ld miel=%ld iter=%d total_exec_ms=%ld\n",
            t, c->id, c->abeja_count, c->huevos, c->polen_acumulado, c->miel, c->pcb.iterations, c->pcb.total_exec_ms);
    fclose(f);
}

static void procesar_actividad(colmena_t *c) {
    int trips = c->abeja_count;
    if (trips <= 0) return;
    for (int i = 0; i < trips; ++i) {
        if (!c->running) break;
        int idx = rand_range(0, (c->abeja_count - 1));
        abeja_t *a = &c->abejas[idx];
        if (!a->alive) continue;
        if (a->tipo == QUEEN) continue;
        int carry = rand_range(1,5);
        int travel_ms = rand_range(1,5);
        usleep(travel_ms * 1000);
        pthread_mutex_lock(&c->lock);
        c->polen_acumulado += carry;
        a->pollen_collected += carry;
        int threshold = rand_range(100,150);
        if (a->pollen_collected >= threshold) {
            a->alive = false;
        }
        pthread_mutex_unlock(&c->lock);
        producir_miel_desde_polen(c);
    }

    int alive = 0;
    pthread_mutex_lock(&c->lock);
    for (int i = 0; i < c->abeja_count; ++i) {
        if (c->abejas[i].alive) {
            c->abejas[alive++] = c->abejas[i];
        }
    }
    c->abeja_count = alive;
    pthread_mutex_unlock(&c->lock);

    int nuevos_huevos = rand_range(0, 2);
    if (nuevos_huevos > 0) {
        pthread_mutex_lock(&c->lock);
        c->huevos += nuevos_huevos;
        pthread_mutex_unlock(&c->lock);
    }
}

static void* colmena_thread_fn(void *arg) {
    colmena_t *c = (colmena_t*)arg;
    long last_log = now_ms();
    while (1) {
        pthread_mutex_lock(&c->lock);
        while (!c->running && c->alive) {
            pthread_cond_wait(&c->cond, &c->lock);
        }
        if (!c->alive) {
            pthread_mutex_unlock(&c->lock);
            break;
        }
        c->pcb.iterations++;
        c->pcb.last_start_ms = now_ms();
        pthread_mutex_unlock(&c->lock);

        while (1) {
            pthread_mutex_lock(&c->lock);
            bool run = c->running;
            pthread_mutex_unlock(&c->lock);
            if (!run) break;

            pthread_mutex_lock(&c->lock);
            hatch_eggs(c);
            pthread_mutex_unlock(&c->lock);

            procesar_actividad(c);

            pthread_mutex_lock(&c->lock);
            producir_miel_desde_polen(c);
            pthread_mutex_unlock(&c->lock);

            usleep(1000);

            if (now_ms() - last_log >= 1000) {
                pthread_mutex_lock(&c->lock);
                escribir_log_colmena_interno(c);
                pthread_mutex_unlock(&c->lock);
                last_log = now_ms();
            }
        }

        long end = now_ms();
        pthread_mutex_lock(&c->lock);
        long delta = end - c->pcb.last_start_ms;
        c->pcb.total_exec_ms += delta;
        pthread_mutex_unlock(&c->lock);
    }

    pthread_mutex_lock(&c->lock);
    escribir_log_colmena_interno(c);
    pthread_mutex_unlock(&c->lock);
    return NULL;
}

colmena_t* crear_colmena(int id) {
    colmena_t *c = calloc(1, sizeof(colmena_t));
    if (!c) return NULL;
    c->id = id;
    pthread_mutex_init(&c->lock, NULL);
    pthread_cond_init(&c->cond, NULL);
    c->running = false;
    c->alive = true;
    c->abeja_capacity = 0;
    c->abejas = NULL;
    c->huevos = rand_range(20,40);
    c->miel = rand_range(20,40);
    c->polen_acumulado = 0;
    int init_abejas = rand_range(20,40);
    asegurar_capacity(c, init_abejas);
    c->abeja_count = 0;
    for (int i = 0; i < init_abejas; ++i) agregar_abeja(c, WORKER);
    c->pcb.id = id;
    c->pcb.arrival_ms = now_ms();
    c->pcb.iterations = 0;
    c->pcb.total_exec_ms = 0;
    snprintf(c->logfile, sizeof(c->logfile), "./var/colmena/colmena_%d.log", id);

    FILE *f = fopen(c->logfile, "w");
    if (f) {
        fprintf(f, "Inicio colmena %d ts=%ld\n", id, now_ms());
        fclose(f);
    }

    return c;
}

void iniciar_colmena(colmena_t *c) {
    pthread_create(&c->thread, NULL, colmena_thread_fn, c);
}

void detener_colmena(colmena_t *c) {
    pthread_mutex_lock(&c->lock);
    c->alive = false;
    c->running = false;
    pthread_cond_signal(&c->cond);
    pthread_mutex_unlock(&c->lock);
    pthread_join(c->thread, NULL);
    free(c->abejas);
    pthread_mutex_destroy(&c->lock);
    pthread_cond_destroy(&c->cond);
    free(c);
}

int obtener_abejas(colmena_t *c) {
    int v;
    pthread_mutex_lock(&c->lock);
    v = c->abeja_count;
    pthread_mutex_unlock(&c->lock);
    return v;
}

long obtener_miel(colmena_t *c) {
    long v;
    pthread_mutex_lock(&c->lock);
    v = c->miel;
    pthread_mutex_unlock(&c->lock);
    return v;
}

int obtener_huevos(colmena_t *c) {
    int v;
    pthread_mutex_lock(&c->lock);
    v = c->huevos;
    pthread_mutex_unlock(&c->lock);
    return v;
}

void set_running(colmena_t *c, bool run) {
    pthread_mutex_lock(&c->lock);
    c->running = run;
    if (run) {
        pthread_cond_signal(&c->cond);
    }
    pthread_mutex_unlock(&c->lock);
}

void escribir_log_colmena(colmena_t *c) {
    pthread_mutex_lock(&c->lock);
    escribir_log_colmena_interno(c);
    pthread_mutex_unlock(&c->lock);
}
