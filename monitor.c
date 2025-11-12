#include "monitor.h"
#include "planificador.h"
#include "colmena.h"
#include "utils.h"
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

extern colmena_t *head;
extern pthread_mutex_t list_lock;

static pthread_t mon_thread;
static int mon_running = 0;

static void print_header() {
    printf("=== MONITOR COLMENAS (Ctrl+C para salir) ===\n");
    printf("%-4s %-8s %-6s %-6s %-8s %-8s\n", "ID", "ABEJAS", "HUEVOS", "MIEL", "ITER", "EXEC(ms)");
}

static void* monitor_fn(void *arg) {
    (void)arg;
    while (mon_running) {
        printf("\033[2J\033[H");
        print_header();
        pthread_mutex_lock(&list_lock);
        colmena_t *p = head;
        while (p) {
            pthread_mutex_lock(&p->lock);
            int abe = p->abeja_count;
            int hue = p->huevos;
            long miel = p->miel;
            int iter = p->pcb.iterations;
            long exec = p->pcb.total_exec_ms;
            printf("%-4d %-8d %-6d %-6ld %-8d %-8ld\n", p->id, abe, hue, miel, iter, exec);
            pthread_mutex_unlock(&p->lock);
            p = p->next;
        }
        pthread_mutex_unlock(&list_lock);

        sched_policy_t pol = planificador_get_policy();
        char *pol_s = (pol==POLICY_RR) ? "RR" : (pol==POLICY_SJF) ? "SJF" : "DYN";
        printf("\nPolicy: %s\n", pol_s);
        sleep(1);
    }
    return NULL;
}

void monitor_start() {
    mon_running = 1;
    pthread_create(&mon_thread, NULL, monitor_fn, NULL);
}

void monitor_stop() {
    mon_running = 0;
    pthread_join(mon_thread, NULL);
}
