#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>

#include <stdlib.h>  
#include <time.h>     


#include "io.h"
#include "utils.h"
#include "colmena.h"
#include "planificador.h"
#include "monitor.h"

pthread_t input_thread;

extern colmena_t *head; 
extern pthread_mutex_t list_lock;


// Presionar:  p  → cambio de política (RR → SJF → DYN → RR)
void *input_thread_fn(void *arg)
{
    (void)arg;

    while (1)
    {
        int ch = getchar_unlocked(); \
        if (ch == EOF)
            continue;

        if (ch == 'p' || ch == 'P')
        {
            printf("\n[INPUT] Cambio manual de política solicitado.\n");
            planificador_switch_policy_manual();
        }

        usleep(50000); // aliviana carga del CPU
    }

    return NULL;
}



static void sigint_handler(int s)
{
    (void)s;
    printf("\nSIGINT recibido. Terminando...\n");

    monitor_stop();
    planificador_stop();
    io_shutdown();

    exit(0);
}


int main(int argc, char **argv)
{
    srand((unsigned int)time(NULL));

    mkdir_if_needed("./var");
    mkdir_if_needed("./var/colmena");

    head = NULL;
    list_lock = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;

    io_init();

    // Crear colmena inicial
    colmena_t *c1 = crear_colmena(1);
    iniciar_colmena(c1);
    planificador_add_colmena(c1);

    // Iniciar planificador y monitor
    planificador_start();
    monitor_start();

    // Registrar SIGINT (Ctrl+C)
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigaction(SIGINT, &sa, NULL);

    // Iniciar hilo de entrada por teclado
    pthread_create(&input_thread, NULL, input_thread_fn, NULL);

    // Esperar señales
    while (1)
        pause();

    return 0;
}
