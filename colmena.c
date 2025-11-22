#define _GNU_SOURCE
#include "colmena.h"
#include "utils.h"
#include "io.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

static void asegurar_capacity(colmena_t *c, int extra)
{
    if (c->abeja_count + extra > c->abeja_capacity)
    {
        int newcap = (c->abeja_capacity == 0) ? 64 : c->abeja_capacity * 2;
        while (newcap < c->abeja_count + extra)
            newcap *= 2;
        abeja_t *n = realloc(c->abejas, sizeof(abeja_t) * newcap);
        if (!n)
        {
            perror("realloc abeja");
            exit(1);
        }
        c->abejas = n;
        c->abeja_capacity = newcap;
    }
}

static void agregar_abeja(colmena_t *c, abeja_tipo t)
{
    asegurar_capacity(c, 1);
    abeja_t *a = &c->abejas[c->abeja_count++];
    a->tipo = t;
    a->pollen_collected = 0;
    a->alive = true;
}

//Eclosion de los huevos pero ahira en la matriz 10×10
static void hatch_eggs(colmena_t *c)
{
    int centro_ini = 4;
    int centro_fin = 5;

    // Eclosionan algunos huevos de las celdas centrales
    for (int i = centro_ini; i <= centro_fin; i++)
    {
        for (int j = centro_ini; j <= centro_fin; j++)
        {
            int *cell = &c->camara[i][j].contenido;

            if (*cell <= 0)
                continue;

            // Cantidad de huevos que nacen 
            int hatch = rand_range(0, *cell);
            if (hatch <= 0)
                continue;

            for (int h = 0; h < hatch; h++)
            {
                int r = rand_range(1, 1000);
                if (r <= 10)
                    agregar_abeja(c, QUEEN);
                else
                    agregar_abeja(c, WORKER);
            }

            // Reducir huevos en la celda
            *cell -= hatch;
        }
    }

    // Recalcular el total de huevos a partir de la matriz central
    int total = 0;
    for (int i = centro_ini; i <= centro_fin; i++)
        for (int j = centro_ini; j <= centro_fin; j++)
            total += c->camara[i][j].contenido;

    c->huevos = total;
}

//Produccion de miel pero esta vez en la matriz
static void producir_miel_desde_polen(colmena_t *c)
{
    for (int i = 0; i < 10; i++)
    {
        for (int j = 0; j < 10; j++)
        {
            int *cell = &c->camara[i][j].contenido;

            if (*cell >= 10)
            {
                int miel_nueva = *cell / 10;
                c->miel += miel_nueva;
                *cell -= miel_nueva * 10;
            }
        }
    }
}

static void escribir_log_colmena_interno(colmena_t *c)
{
    FILE *f = fopen(c->logfile, "a");
    if (!f)
        return;
    long t = now_ms();
    fprintf(f, "ts=%ld id=%d abejas=%d huevos=%d polen=%ld miel=%ld iter=%d total_exec_ms=%ld\n",
            t, c->id, c->abeja_count, c->huevos, c->polen_acumulado, c->miel,
            c->pcb.iterations, c->pcb.total_exec_ms);
    fclose(f);
}

//Actividad de la colmena
static void procesar_actividad(colmena_t *c)
{
    int trips = c->abeja_count;
    // Limitar trabajo por llamada para no bloquear por miles de abejas
    if (trips > 20)
        trips = 20;

    if (trips <= 0)
        return;

    for (int i = 0; i < trips; ++i)
    {
        if (!c->running || !c->alive)
            break;

        int idx = rand_range(0, (c->abeja_count - 1));
        abeja_t *a = &c->abejas[idx];
        if (!a->alive)
            continue;
        if (a->tipo == QUEEN)
            continue;

        int carry = rand_range(1, 5);
        int travel_ms = rand_range(1, 5);

        // E/S simulada por io
        io_solicitar(c, travel_ms);

        pthread_mutex_lock(&c->lock);

        c->polen_acumulado += carry;
        a->pollen_collected += carry;

        // Depositar polen en celda aleatoria 1de la matriz
        int x = rand_range(0, 9);
        int y = rand_range(0, 9);
        c->camara[x][y].contenido += carry;

        int threshold = rand_range(100, 150);
        if (a->pollen_collected >= threshold)
        {
            a->alive = false;
        }

        pthread_mutex_unlock(&c->lock);

        producir_miel_desde_polen(c);
    }

    // Eliminar abejas muertas
    int alive = 0;
    pthread_mutex_lock(&c->lock);
    for (int i = 0; i < c->abeja_count; ++i)
    {
        if (c->abejas[i].alive)
        {
            c->abejas[alive++] = c->abejas[i];
        }
    }
    c->abeja_count = alive;
    pthread_mutex_unlock(&c->lock);

    //nuevos huevos en la matriz
    int nuevos_huevos = rand_range(0, 2);
    if (nuevos_huevos > 0)
    {
        pthread_mutex_lock(&c->lock);

        for (int h = 0; h < nuevos_huevos; h++)
        {
            int x = rand_range(4, 5);
            int y = rand_range(4, 5);
            c->camara[x][y].contenido += 1;
        }

        c->huevos += nuevos_huevos;
        pthread_mutex_unlock(&c->lock);
    }
}

//El hilo principal de la colmena
static void *colmena_thread_fn(void *arg)
{
    colmena_t *c = (colmena_t *)arg;
    long last_log = now_ms();

    while (1)
    {
        pthread_mutex_lock(&c->lock);

        // La colmena entra a READY
        long t_ready_start = now_ms();

        while (!c->running && c->alive)
        {
            pthread_cond_wait(&c->cond, &c->lock);
        }

        long t_ready_end = now_ms();
        long waited = t_ready_end - t_ready_start;

        if (waited > 0)
        {
            c->pcb.ready_wait_ms += waited;
            c->pcb.ready_count++;
            c->pcb.avg_ready_wait_ms = c->pcb.ready_wait_ms / c->pcb.ready_count;
        }

        if (!c->alive)
        {
            pthread_mutex_unlock(&c->lock);
            break;
        }

        c->pcb.iterations++;
        c->pcb.code_progress += rand_range(1, 3);

        c->pcb.last_start_ms = now_ms();
        pthread_mutex_unlock(&c->lock);

        // Quantum 
        for (int step = 0; step < 5; ++step)
        {
            pthread_mutex_lock(&c->lock);
            bool run = c->running;
            bool alive = c->alive;
            pthread_mutex_unlock(&c->lock);

            if (!alive)
                goto fin_total;
            if (!run)
                break;

            // 1) Eclosionar huevos
            pthread_mutex_lock(&c->lock);
            hatch_eggs(c);
            pthread_mutex_unlock(&c->lock);

            // 2) Procesar actividad 
            procesar_actividad(c);

            // 3) Producir miel desde la matriz
            pthread_mutex_lock(&c->lock);
            producir_miel_desde_polen(c);
            pthread_mutex_unlock(&c->lock);

            // pausa 
            usleep(1000);

            // Logging cada 1 segundo
            if (now_ms() - last_log >= 1000)
            {
                pthread_mutex_lock(&c->lock);
                escribir_log_colmena_interno(c);
                pthread_mutex_unlock(&c->lock);
                last_log = now_ms();
            }
        }

        // Actualizar tiempo total de ejecución de este quantum
        long end = now_ms();
        pthread_mutex_lock(&c->lock);
        long delta = end - c->pcb.last_start_ms;
        if (delta > 0)
            c->pcb.total_exec_ms += delta;
        pthread_mutex_unlock(&c->lock);
    }

fin_total:
    pthread_mutex_lock(&c->lock);
    escribir_log_colmena_interno(c);
    pthread_mutex_unlock(&c->lock);
    return NULL;
}

//La creacion y el control de la colmena
colmena_t *crear_colmena(int id)
{
    colmena_t *c = calloc(1, sizeof(colmena_t));
    if (!c)
        return NULL;

    c->id = id;
    pthread_mutex_init(&c->lock, NULL);
    pthread_cond_init(&c->cond, NULL);
    pthread_cond_init(&c->io_cond, NULL);  
    c->waiting_io = false;
    c->running = false;
    c->alive = true;

    c->abeja_capacity = 0;
    c->abejas = NULL;

    for (int i = 0; i < 10; i++)
        for (int j = 0; j < 10; j++)
            c->camara[i][j].contenido = 0;

    // Inicializar huevos iniciales entre 20 y 40
    c->huevos = rand_range(20, 40);

    // Distribuirlos en la zona central de la cámara 10×10
    int huevos_tmp = c->huevos;

    int centro_ini = 4;
    int centro_fin = 5;

    for (int i = centro_ini; i <= centro_fin; i++)
    {
        for (int j = centro_ini; j <= centro_fin; j++)
        {
            if (huevos_tmp <= 0)
                break;

            int agregar = rand_range(1, 3);
            if (agregar > huevos_tmp)
                agregar = huevos_tmp;

            c->camara[i][j].contenido += agregar;
            huevos_tmp -= agregar;
        }
    }

    c->miel = rand_range(20, 40);
    c->polen_acumulado = 0;

    int init_abejas = rand_range(20, 40);
    asegurar_capacity(c, init_abejas);
    c->abeja_count = 0;

    for (int i = 0; i < init_abejas; ++i)
        agregar_abeja(c, WORKER);

    c->pcb.id = id;
    c->pcb.arrival_ms = now_ms();
    c->pcb.iterations = 0;
    c->pcb.total_exec_ms = 0;
    c->pcb.io_wait_ms = 0;
    c->pcb.avg_io_wait_ms = 0;
    c->pcb.ready_wait_ms = 0;
    c->pcb.avg_ready_wait_ms = 0;
    c->pcb.ready_count = 0;
    c->pcb.io_count = 0;
    c->pcb.code_progress = 0;
    c->pcb.last_quantum_ms = 0;

    snprintf(c->logfile, sizeof(c->logfile), "./var/colmena/colmena_%d.log", id);

    FILE *f = fopen(c->logfile, "w");
    if (f)
    {
        fprintf(f, "Inicio colmena %d ts=%ld\n", id, now_ms());
        fclose(f);
    }

    return c;
}

void iniciar_colmena(colmena_t *c)
{
    pthread_create(&c->thread, NULL, colmena_thread_fn, c);
}

void detener_colmena(colmena_t *c)
{
    pthread_mutex_lock(&c->lock);
    c->alive = false;
    c->running = false;
    pthread_cond_signal(&c->cond);
    pthread_cond_broadcast(&c->io_cond); // por si alguna vez espera por io
    pthread_mutex_unlock(&c->lock);

    pthread_join(c->thread, NULL);
    free(c->abejas);
    pthread_mutex_destroy(&c->lock);
    pthread_cond_destroy(&c->cond);
    pthread_cond_destroy(&c->io_cond);

    free(c);
}

int obtener_abejas(colmena_t *c)
{
    return c->abeja_count;
}

long obtener_miel(colmena_t *c)
{
    return c->miel;
}

int obtener_huevos(colmena_t *c)
{
    return c->huevos;
}

void set_running(colmena_t *c, bool run)
{
    pthread_mutex_lock(&c->lock);
    c->running = run;
    if (run)
        pthread_cond_signal(&c->cond);
    pthread_mutex_unlock(&c->lock);
}

void escribir_log_colmena(colmena_t *c)
{
    pthread_mutex_lock(&c->lock);
    escribir_log_colmena_interno(c);
    pthread_mutex_unlock(&c->lock);
}
