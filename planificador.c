#define _GNU_SOURCE
#include "planificador.h"
#include "utils.h"
#include "tabla_procesos.h"

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <limits.h>

global_stats_t gstats;

colmena_t *head = NULL;
pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER;

static pthread_t sched_thread;
static int next_id = 1;
static bool running = false;

static sched_policy_t current_policy = POLICY_RR;
static int rr_quantum_ms = 5;
static int rr_cycle_count = 0;
static int rr_cycles_to_adjust = 5;

//un helper que asegura la cantidad de las reinas
static void asegurar_capacity_colmena(colmena_t *c, int extra)
{
    if (c->abeja_count + extra > c->abeja_capacity)
    {
        int newcap = (c->abeja_capacity == 0) ? 64 : c->abeja_capacity * 2;
        while (newcap < c->abeja_count + extra)
            newcap *= 2;

        abeja_t *n = realloc(c->abejas, sizeof(abeja_t) * newcap);
        if (!n)
        {
            perror("realloc abeja (planificador)");
            exit(1);
        }
        c->abejas = n;
        c->abeja_capacity = newcap;
    }
}


void planificador_add_colmena(colmena_t *c)
{
    pthread_mutex_lock(&list_lock);
    c->next = NULL;
    if (!head)
        head = c;
    else
    {
        colmena_t *p = head;
        while (p->next)
            p = p->next;
        p->next = c;
    }
    pthread_mutex_unlock(&list_lock);
}

void planificador_remove_colmena(int id)
{
    pthread_mutex_lock(&list_lock);
    colmena_t **pp = &head;
    while (*pp)
    {
        if ((*pp)->id == id)
        {
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

sched_policy_t planificador_get_policy()
{
    return current_policy;
}


void cambiar_quantum_random()
{
    rr_quantum_ms = rand_range(2, 10);
    gstats.quantum_actual = rr_quantum_ms;
}

//selectores de politica

static colmena_t *elegir_sjf()
{
    pthread_mutex_lock(&list_lock);
    colmena_t *best = NULL;
    long best_val = LONG_MAX;

    for (colmena_t *p = head; p; p = p->next)
    {
        pthread_mutex_lock(&p->lock);
        long metric = p->abeja_count + p->miel;
        pthread_mutex_unlock(&p->lock);

        if (metric < best_val)
        {
            best_val = metric;
            best = p;
        }
    }

    pthread_mutex_unlock(&list_lock);
    return best;
}

static colmena_t *elegir_dyn()
{
    pthread_mutex_lock(&list_lock);
    colmena_t *best = NULL;
    long best_prio = LONG_MAX;

    for (colmena_t *p = head; p; p = p->next)
    {
        pthread_mutex_lock(&p->lock);
        long prio = p->miel + p->abeja_count * 2;
        pthread_mutex_unlock(&p->lock);

        if (prio < best_prio)
        {
            best_prio = prio;
            best = p;
        }
    }

    pthread_mutex_unlock(&list_lock);
    return best;
}

static colmena_t *elegir_rr(colmena_t **last_rr)
{
    pthread_mutex_lock(&list_lock);

    colmena_t *p = (*last_rr) ? (*last_rr)->next : head;
    if (!p)
        p = head;
    if (!p)
    {
        pthread_mutex_unlock(&list_lock);
        return NULL;
    }

    colmena_t *sel = p;
    *last_rr = sel;

    pthread_mutex_unlock(&list_lock);
    return sel;
}

//Cambio de politica por se;al

static void sigusr1_handler(int signo)
{
    (void)signo;

    if (current_policy == POLICY_RR)
        current_policy = POLICY_SJF;
    else if (current_policy == POLICY_SJF)
        current_policy = POLICY_DYN;
    else
        current_policy = POLICY_RR;

    cambiar_quantum_random();
}

void planificador_switch_policy_manual()
{
    sigusr1_handler(0);
}

//La migracion al crear una nueva reina

static void revisar_reinas_y_split(void)
{
    pthread_mutex_lock(&list_lock);

    for (colmena_t *p = head; p; p = p->next)
    {
        pthread_mutex_lock(&p->lock);

        bool found_queen = false;
        for (int i = 0; i < p->abeja_count; ++i)
        {
            if (p->abejas[i].tipo == QUEEN)
            {
                //La reina se queda como WORKER en la colmena madre
                p->abejas[i].tipo = WORKER;
                found_queen = true;
                break;
            }
        }

        if (!found_queen)
        {
            pthread_mutex_unlock(&p->lock);
            continue;
        }

        //Crear nueva colmena hija 
        int nid = ++next_id;
        colmena_t *nc = crear_colmena(nid);

        pthread_mutex_lock(&nc->lock);

        //Limpiar estado aleatorio inicial de la hija 
        free(nc->abejas);
        nc->abejas = NULL;
        nc->abeja_capacity = 0;
        nc->abeja_count = 0;

        nc->huevos = 0;
        nc->miel = 0;
        nc->polen_acumulado = 0;
        for (int x = 0; x < 10; ++x)
            for (int y = 0; y < 10; ++y)
                nc->camara[x][y].contenido = 0;

        //1) Migrar la mitad de las abejas vivas 
        int transfer_bees = p->abeja_count / 2;
        for (int t = 0; t < transfer_bees; ++t)
        {
            if (p->abeja_count <= 0)
                break;

            abeja_t mover = p->abejas[--p->abeja_count];

            if (!mover.alive)
                continue;

            asegurar_capacity_colmena(nc, 1);
            nc->abejas[nc->abeja_count++] = mover;
        }

        //2) Migrar 1/3 de la miel 
        long transfer_miel = p->miel / 3;
        p->miel -= transfer_miel;
        nc->miel += transfer_miel;

        //3) Migrar 1/3 de los huevos 
        int transfer_huevos = p->huevos / 3;
        p->huevos -= transfer_huevos;
        nc->huevos += transfer_huevos;

        //4) Migrar 1/3 del polen acumulado
        long transfer_polen = p->polen_acumulado / 3;
        p->polen_acumulado -= transfer_polen;
        nc->polen_acumulado += transfer_polen;

        // 5) Migrar 1/3 del contenido de la cámara 
        for (int x = 0; x < 10; ++x)
        {
            for (int y = 0; y < 10; ++y)
            {
                int val = p->camara[x][y].contenido;
                int move = val / 3;
                p->camara[x][y].contenido -= move;
                nc->camara[x][y].contenido += move;
            }
        }

        pthread_mutex_unlock(&p->lock);
        pthread_mutex_unlock(&nc->lock);

        //añadimos la nueva colmena a la lista global, si modifican no llamen al  planificador_add_colmena por que es lo que hacia el bloqueo infinto
        nc->next = head;
        head = nc;

        //arrancasmo un hulo
        iniciar_colmena(nc);
    }

    pthread_mutex_unlock(&list_lock);
}

//hilo del planificado

static void *sched_thread_fn(void *arg)
{
    (void)arg;
    colmena_t *last_rr = NULL;

    while (running)
    {
        colmena_t *selected = NULL;

        if (current_policy == POLICY_RR)
        {
            selected = elegir_rr(&last_rr);

            if (!selected)
            {
                usleep(1000);
                tabla_global_actualizar();
                continue;
            }

            gstats.sched_cycles++;
            gstats.last_selected_id = selected->id;
            gstats.quantum_actual = rr_quantum_ms;

            //RR: activar solo la colmena seleccionada 
            pthread_mutex_lock(&list_lock);
            for (colmena_t *p = head; p; p = p->next)
            {
                set_running(p, p == selected);
            }
            pthread_mutex_unlock(&list_lock);

            gstats.context_switches++;

            usleep(rr_quantum_ms * 1000);

            rr_cycle_count++;
            if (rr_cycle_count >= rr_cycles_to_adjust)
            {
                cambiar_quantum_random();
                rr_cycle_count = 0;
            }
        }
        else if (current_policy == POLICY_SJF)
        {
            selected = elegir_sjf();

            if (!selected)
            {
                usleep(1000);
                tabla_global_actualizar();
                continue;
            }

            gstats.sched_cycles++;
            gstats.last_selected_id = selected->id;
            gstats.quantum_actual = 8;

            pthread_mutex_lock(&list_lock);
            for (colmena_t *p = head; p; p = p->next)
            {
                set_running(p, p == selected);
            }
            pthread_mutex_unlock(&list_lock);

            gstats.context_switches++;

            usleep(8 * 1000);
        }
        else if (current_policy == POLICY_DYN)
        {
            selected = elegir_dyn();

            if (!selected)
            {
                usleep(1000);
                tabla_global_actualizar();
                continue;
            }

            gstats.sched_cycles++;
            gstats.last_selected_id = selected->id;

            int q = 5;
            pthread_mutex_lock(&selected->lock);
            long miel = selected->miel;
            pthread_mutex_unlock(&selected->lock);

            if (miel < 10)
                q = 12;
            else if (miel < 30)
                q = 8;
            else
                q = 4;

            gstats.quantum_actual = q;

            pthread_mutex_lock(&list_lock);
            for (colmena_t *p = head; p; p = p->next)
            {
                set_running(p, p == selected);
            }
            pthread_mutex_unlock(&list_lock);

            gstats.context_switches++;

            usleep(q * 1000);
        }

        //Split de colmenas cuando aparezca reina 
        revisar_reinas_y_split();

        //Actualizar tabla global de procesos 
        tabla_global_actualizar();

        usleep(500);
    }

    //apagar todas las colmenas al salir 
    pthread_mutex_lock(&list_lock);
    for (colmena_t *p = head; p; p = p->next)
        set_running(p, false);
    pthread_mutex_unlock(&list_lock);

    return NULL;
}



void planificador_start()
{
    running = true;
    memset(&gstats, 0, sizeof(gstats));
    gstats.quantum_actual = rr_quantum_ms;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigusr1_handler;
    sigaction(SIGUSR1, &sa, NULL);

    pthread_create(&sched_thread, NULL, sched_thread_fn, NULL);
}

void planificador_stop()
{
    running = false;
    pthread_join(sched_thread, NULL);

    pthread_mutex_lock(&list_lock);
    colmena_t *p = head;
    while (p)
    {
        colmena_t *n = p->next;
        detener_colmena(p);
        p = n;
    }
    head = NULL;
    pthread_mutex_unlock(&list_lock);
}
