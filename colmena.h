#ifndef COLMENA_H
#define COLMENA_H

#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>

typedef enum { WORKER, QUEEN } abeja_tipo;

typedef struct {
    abeja_tipo tipo;
    int pollen_collected;
    bool alive;
} abeja_t;

typedef struct pcb {
    int id;
    long arrival_ms;
    long last_start_ms;
    long total_exec_ms;
    int iterations;
    long io_wait_ms;
    long avg_io_wait_ms;
    long ready_wait_ms;
    long avg_ready_wait_ms;
} pcb_t;

typedef struct colmena {
    int id;
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    bool running;

    abeja_t *abejas;
    int abeja_count;
    int abeja_capacity;

    int huevos;
    long miel;
    long polen_acumulado;

    pcb_t pcb;

    bool alive;
    struct colmena *next;

    char logfile[256];
} colmena_t;

colmena_t* crear_colmena(int id);
void iniciar_colmena(colmena_t *c);
void detener_colmena(colmena_t *c);
int obtener_abejas(colmena_t *c);
long obtener_miel(colmena_t *c);
int obtener_huevos(colmena_t *c);
void set_running(colmena_t *c, bool run);
void escribir_log_colmena(colmena_t *c);

#endif // COLMENA_H
