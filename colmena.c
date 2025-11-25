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

// Helpers de abejas

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

static void agregar_huevos_celda(colmena_t *c, int x, int y, int cantidad)
{
    celda_t *cell = &c->camara[x][y];
    cell->contenido += cantidad;
    cell->egg_birth_ms = now_ms();
    cell->hatch_delay_ms = rand_range(1, 10); // 1-10 ms
}

// Eclosión de huevos en la matriz 10x10 (zona central)
static void hatch_eggs(colmena_t *c)
{
    int centro_ini = 4;
    int centro_fin = 5;
    long now = now_ms();
    int total = 0;

    for (int i = centro_ini; i <= centro_fin; i++)
    {
        for (int j = centro_ini; j <= centro_fin; j++)
        {
            celda_t *cell = &c->camara[i][j];

            if (cell->contenido <= 0)
                continue;

            if (cell->egg_birth_ms == 0)
            {
                cell->egg_birth_ms = now;
                cell->hatch_delay_ms = rand_range(1, 10);
            }

            long age = now - cell->egg_birth_ms;

            if (age < cell->hatch_delay_ms)
            {
                total += cell->contenido;
                continue;
            }

            int hatch = rand_range(0, cell->contenido);

            if (hatch > 0)
            {
                for (int h = 0; h < hatch; h++)
                {
                    int r = rand_range(1, 1000);
                    agregar_abeja(c, (r <= 10) ? QUEEN : WORKER);
                }

                cell->contenido -= hatch;

                if (cell->contenido > 0)
                {
                    cell->egg_birth_ms = now;
                    cell->hatch_delay_ms = rand_range(1, 10);
                }
                else
                {
                    cell->egg_birth_ms = 0;
                    cell->hatch_delay_ms = 0;
                }
            }

            total += cell->contenido;
        }
    }

    c->huevos = total;
}

// Producción de miel desde el polen depositado en la matriz
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

// Logging

static void escribir_log_colmena_interno(colmena_t *c)
{
    FILE *f = fopen(c->logfile, "a");
    if (!f)
        return;

    long t = now_ms();
    fprintf(f,
            "ts=%ld id=%d abejas=%d huevos=%d polen=%ld miel=%ld iter=%d total_exec_ms=%ld\n",
            t,
            c->id,
            c->abeja_count,
            c->huevos,
            c->polen_acumulado,
            c->miel,
            c->pcb.iterations,
            c->pcb.total_exec_ms);
    fclose(f);
}

// SUBHILOS DE LA COLMENA

// Hilo de recolección de polen
static void *hilo_recoleccion_fn(void *arg)
{
    colmena_t *c = (colmena_t *)arg;

    while (c->sub_hilos_activos && c->alive)
    {
        pthread_mutex_lock(&c->lock);
        bool run = c->running;
        int abeja_count = c->abeja_count;
        pthread_mutex_unlock(&c->lock);

        if (!run)
        {
            usleep(1000);
            continue;
        }

        int trips = abeja_count;
        if (trips > 15)
            trips = 15;

        for (int i = 0; i < trips; i++)
        {
            pthread_mutex_lock(&c->lock);
            if (!c->alive || !c->running || c->abeja_count <= 0)
            {
                pthread_mutex_unlock(&c->lock);
                break;
            }

            int idx = rand_range(0, c->abeja_count - 1);
            abeja_t *a = &c->abejas[idx];

            if (!a->alive || a->tipo == QUEEN)
            {
                pthread_mutex_unlock(&c->lock);
                continue;
            }

            int carry = rand_range(1, 5);
            int travel_ms = rand_range(1, 5);

            pthread_mutex_unlock(&c->lock);

            // E/S simulada
            io_solicitar(c, travel_ms);

            pthread_mutex_lock(&c->lock);

            if (!c->alive)
            {
                pthread_mutex_unlock(&c->lock);
                break;
            }

            c->polen_acumulado += carry;
            a->pollen_collected += carry;

            int x = rand_range(0, 9);
            int y = rand_range(0, 9);
            c->camara[x][y].contenido += carry;

            int threshold = rand_range(100, 150);
            if (a->pollen_collected >= threshold)
                a->alive = false;

            pthread_mutex_unlock(&c->lock);
        }

        // Compactar abejas vivas
        pthread_mutex_lock(&c->lock);
        int alive = 0;
        for (int i = 0; i < c->abeja_count; ++i)
        {
            if (c->abejas[i].alive)
                c->abejas[alive++] = c->abejas[i];
        }
        c->abeja_count = alive;
        pthread_mutex_unlock(&c->lock);

        usleep(2000); // pequeña pausa entre rondas de recolección
    }

    return NULL;
}

// Hilo de producción de miel
static void *hilo_miel_fn(void *arg)
{
    colmena_t *c = (colmena_t *)arg;

    while (c->sub_hilos_activos && c->alive)
    {
        pthread_mutex_lock(&c->lock);
        bool run = c->running;
        if (run)
        {
            producir_miel_desde_polen(c);
        }
        pthread_mutex_unlock(&c->lock);

        usleep(3000); // intervalo de producción de miel
    }

    return NULL;
}

// Hilo de eclosión de huevos
static void *hilo_huevos_fn(void *arg)
{
    colmena_t *c = (colmena_t *)arg;

    while (c->sub_hilos_activos && c->alive)
    {
        pthread_mutex_lock(&c->lock);

        if (c->running)
        {
            // crear nuevos huevos ocasionales
            int nuevos = rand_range(0, 2);
            for (int h = 0; h < nuevos; h++)
            {
                int x = rand_range(4, 5);
                int y = rand_range(4, 5);
                agregar_huevos_celda(c, x, y, 1);
            }

            // procesar eclosión real por tiempo
            hatch_eggs(c);
        }

        pthread_mutex_unlock(&c->lock);

        usleep(1000); // 1 ms poll interval
    }

    return NULL;
}

// HILO PRINCIPAL DE LA COLMENA

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

            usleep(1000); // trabajo simulado de CPU

            // Logging cada 1 segundo
            if (now_ms() - last_log >= 1000)
            {
                pthread_mutex_lock(&c->lock);
                escribir_log_colmena_interno(c);
                pthread_mutex_unlock(&c->lock);
                last_log = now_ms();
            }
        }

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

// Creación y control de colmenas

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
    c->sub_hilos_activos = false;

    c->abeja_capacity = 0;
    c->abejas = NULL;

    // Inicializar todas las celdas de la cámara
    for (int i = 0; i < 10; i++)
    {
        for (int j = 0; j < 10; j++)
        {
            c->camara[i][j].contenido = 0;
            c->camara[i][j].egg_birth_ms = 0;
            c->camara[i][j].hatch_delay_ms = 0;
        }
    }

    // Huevos iniciales entre 20 y 40
    c->huevos = rand_range(20, 40);

    int huevos_tmp = c->huevos;
    int centro_ini = 4;
    int centro_fin = 5;

    // Distribuir huevos iniciales en la zona central con temporizador real
    for (int i = centro_ini; i <= centro_fin; i++)
    {
        for (int j = centro_ini; j <= centro_fin; j++)
        {
            if (huevos_tmp <= 0)
                break;

            int agregar = rand_range(1, 3);
            if (agregar > huevos_tmp)
                agregar = huevos_tmp;

            agregar_huevos_celda(c, i, j, agregar);
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

    // PCB inicial
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
    c->sub_hilos_activos = true;

    // Hilo principal de la colmena
    pthread_create(&c->thread, NULL, colmena_thread_fn, c);

    // Subprocesos internos: recolección, miel, huevos
    pthread_create(&c->hilo_recoleccion, NULL, hilo_recoleccion_fn, c);
    pthread_create(&c->hilo_miel, NULL, hilo_miel_fn, c);
    pthread_create(&c->hilo_huevos, NULL, hilo_huevos_fn, c);
}

void detener_colmena(colmena_t *c)
{
    pthread_mutex_lock(&c->lock);
    c->alive = false;
    c->running = false;
    c->sub_hilos_activos = false;
    pthread_cond_broadcast(&c->cond);
    pthread_cond_broadcast(&c->io_cond);
    pthread_mutex_unlock(&c->lock);

    // Esperar todos los hilos
    pthread_join(c->thread, NULL);
    pthread_join(c->hilo_recoleccion, NULL);
    pthread_join(c->hilo_miel, NULL);
    pthread_join(c->hilo_huevos, NULL);

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
