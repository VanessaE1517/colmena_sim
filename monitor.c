#include "monitor.h"
#include "planificador.h"
#include "colmena.h"
#include "utils.h"
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include "tabla_procesos.h"

extern colmena_t *head;
// Ya no usamos list_lock aquí

static pthread_t mon_thread;
static int mon_running = 0;

static void print_header()
{
    printf("=== MONITOR COLMENAS (Ctrl+C para salir) ===\n");
    printf("%-4s %-8s %-6s %-6s %-8s %-8s\n",
           "ID", "ABEJAS", "HUEVOS", "MIEL", "ITER", "EXEC(ms)");
}

static void *monitor_fn(void *arg)
{
    (void)arg;
    while (mon_running)
    {
        // Limpiar pantalla y mover cursor al inicio
        printf("\033[2J\033[H");
        print_header();

        colmena_t *p = head;
        while (p)
        {
            int abe = obtener_abejas(p);
            int hue = obtener_huevos(p);
            long miel = obtener_miel(p);
            int iter = p->pcb.iterations;
            long exec = p->pcb.total_exec_ms;

            printf("%-4d %-8d %-6d %-6ld %-8d %-8ld\n",
                   p->id, abe, hue, miel, iter, exec);

            p = p->next;
        }

        // Mostrar política actual
        sched_policy_t pol = planificador_get_policy();
        char *pol_s = (pol == POLICY_RR) ? (char *)"RR" : (pol == POLICY_SJF) ? (char *)"SJF"
                                                                              : (char *)"DYN";
        printf("\nPolicy: %s\n", pol_s);

        tabla_global_actualizar();
        tabla_global_t tg = tabla_global_obtener();

        printf("\n--- TABLA GLOBAL DE PROCESOS ---\n");
        printf("Colmenas activas: %d\n", tg.num_colmenas);
        printf("Promedio Exec(ms):       %ld\n", tg.avg_exec_ms);
        printf("Promedio IO Wait(ms):    %ld\n", tg.avg_io_wait_ms);
        printf("Promedio Ready Wait(ms): %ld\n", tg.avg_ready_wait_ms);

        printf("Total Abejas:    %ld\n", tg.total_bees);
        printf("Total Miel:      %ld\n", tg.total_honey);
        printf("Total Huevos:    %ld\n", tg.total_eggs);

        printf("\n--- ESTADÍSTICAS DEL PLANIFICADOR ---\n");
        printf("Ciclos ejecutados:       %ld\n", gstats.sched_cycles);
        printf("Cambios de contexto:     %ld\n", gstats.context_switches);
        printf("Última colmena elegida:  %d\n", gstats.last_selected_id);
        printf("Quantum actual:          %d ms\n", gstats.quantum_actual);

        fflush(stdout);
        sleep(1);
    }
    return NULL;
}

void monitor_start()
{
    mon_running = 1;
    pthread_create(&mon_thread, NULL, monitor_fn, NULL);
}

void monitor_stop()
{
    mon_running = 0;
    pthread_join(mon_thread, NULL);
}
