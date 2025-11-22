#include "io.h"
#include "colmena.h"
#include "utils.h"

#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct io_req
{
    colmena_t *colmena;
    int dur_ms;
    long submit_ms;
    struct io_req *next;
} io_req_t;

static pthread_t io_thread;
static pthread_mutex_t io_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t io_cond = PTHREAD_COND_INITIALIZER;
static io_req_t *io_head = NULL;
static io_req_t *io_tail = NULL;
static bool io_running = false;

// Encolar petición
static void enqueue_io(colmena_t *c, int dur_ms)
{
    io_req_t *r = malloc(sizeof(io_req_t));
    if (!r)
        return;
    r->colmena = c;
    r->dur_ms = dur_ms;
    r->submit_ms = now_ms();
    r->next = NULL;

    if (!io_tail)
    {
        io_head = io_tail = r;
    }
    else
    {
        io_tail->next = r;
        io_tail = r;
    }
}

// Desencolar
static io_req_t *dequeue_io(void)
{
    if (!io_head)
        return NULL;
    io_req_t *r = io_head;
    io_head = io_head->next;
    if (!io_head)
        io_tail = NULL;
    return r;
}

static void *io_thread_fn(void *arg)
{
    (void)arg;

    while (1)
    {
        pthread_mutex_lock(&io_lock);
        while (io_running && io_head == NULL)
        {
            pthread_cond_wait(&io_cond, &io_lock);
        }

        if (!io_running && io_head == NULL)
        {
            pthread_mutex_unlock(&io_lock);
            break;
        }

        io_req_t *req = dequeue_io();
        pthread_mutex_unlock(&io_lock);

        if (!req)
            continue;

        colmena_t *c = req->colmena;

        long start = now_ms();
        usleep(req->dur_ms * 1000); // “tiempo de E/S”
        long end = now_ms();
        long delta = end - start;

        // Si la colmena ya murió, no se toca
        pthread_mutex_lock(&c->lock);
        if (!c->alive)
        {
            // ya no actualizamos métricas ni señalizamos
            pthread_mutex_unlock(&c->lock);
            free(req);
            continue;
        }

        // Actualizar métricas de E/S en el PCB
        c->pcb.io_wait_ms += delta;
        c->pcb.io_count++;
        if (c->pcb.io_count > 0)
            c->pcb.avg_io_wait_ms = c->pcb.io_wait_ms / c->pcb.io_count;

        // Marcar que terminó la E/S y despertar al hilo de la colmena
        c->waiting_io = false;
        pthread_cond_signal(&c->io_cond);
        pthread_mutex_unlock(&c->lock);

        free(req);
    }

    return NULL;
}

void io_init(void)
{
    io_running = true;
    pthread_create(&io_thread, NULL, io_thread_fn, NULL);
}

void io_shutdown(void)
{
    pthread_mutex_lock(&io_lock);
    io_running = false;
    pthread_cond_broadcast(&io_cond);
    pthread_mutex_unlock(&io_lock);

    pthread_join(io_thread, NULL);

    // Limpiar cola pendiente si queda algo
    io_req_t *p = io_head;
    while (p)
    {
        io_req_t *n = p->next;
        free(p);
        p = n;
    }
    io_head = io_tail = NULL;
}

void io_solicitar(struct colmena *col, int dur_ms)
{
    colmena_t *c = (colmena_t *)col;
    if (!c)
        return;

    // Si ya está muerta, no pide e/s
    pthread_mutex_lock(&c->lock);
    if (!c->alive)
    {
        pthread_mutex_unlock(&c->lock);
        return;
    }
    c->waiting_io = true;
    pthread_mutex_unlock(&c->lock);

    // Encolar petición
    pthread_mutex_lock(&io_lock);
    enqueue_io(c, dur_ms);
    pthread_cond_signal(&io_cond);
    pthread_mutex_unlock(&io_lock);

    // Esperar a que el termine
    pthread_mutex_lock(&c->lock);
    while (c->waiting_io)
    {
        if (!c->alive)
        {
            // La colmena murió mientras esperaba I/O
            c->waiting_io = false;
            pthread_mutex_unlock(&c->lock);
            return;
        }

        pthread_cond_wait(&c->io_cond, &c->lock);
    }

    // Si terminó I/O normalmente:
    pthread_mutex_unlock(&c->lock);

    pthread_mutex_unlock(&c->lock);
}
