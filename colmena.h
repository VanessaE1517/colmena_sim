#ifndef COLMENA_H
#define COLMENA_H

#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>

typedef enum
{
    WORKER,
    QUEEN
} abeja_tipo;

typedef struct
{
    abeja_tipo tipo;
    int pollen_collected;
    bool alive;
} abeja_t;

typedef struct pcb
{
    int id;

    long arrival_ms;

    long last_start_ms;
    long total_exec_ms;

    int iterations;

    long io_wait_ms;     // tiempo TOTAL de E/S
    long avg_io_wait_ms; // promedio de E/S

    long ready_wait_ms;     // tiempo TOTAL en READY
    long avg_ready_wait_ms; // promedio READY

    int ready_count; // cuántas veces entró a READY
    int io_count;    // cuántas veces hizo E/S

    int code_progress; // avance simulado del "código"
    int last_quantum_ms;
} pcb_t;


typedef struct celda
{
    int contenido;       // >0: algo en la celda (huevos/polen según zona)
    long egg_birth_ms;   // momento en que se agregaron huevos
    long hatch_delay_ms; // tiempo en ms que deben esperar esos huevos

} celda_t;

typedef struct colmena
{
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

    celda_t camara[10][10];

    pcb_t pcb;

    pthread_cond_t io_cond;
    bool waiting_io;

    bool alive;
    struct colmena *next;

    char logfile[256];
    pthread_t hilo_recoleccion;
    pthread_t hilo_miel;
    pthread_t hilo_huevos;

    bool sub_hilos_activos;
} colmena_t;

colmena_t *crear_colmena(int id);
void iniciar_colmena(colmena_t *c);
void detener_colmena(colmena_t *c);
int obtener_abejas(colmena_t *c);
long obtener_miel(colmena_t *c);
int obtener_huevos(colmena_t *c);
void set_running(colmena_t *c, bool run);
void escribir_log_colmena(colmena_t *c);

#endif
