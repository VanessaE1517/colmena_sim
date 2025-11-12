#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "utils.h"
#include "colmena.h"
#include "planificador.h"
#include "monitor.h"

extern colmena_t *head; // usado por monitor.c / planificador.c (extern)
extern pthread_mutex_t list_lock;

static void sigint_handler(int s) {
    (void)s;
    printf("\\nSIGINT recibido. Terminando...\\n");
    planificador_stop();
    monitor_stop();
    exit(0);
}

int main(int argc, char **argv) {
    srand((unsigned int)time(NULL));

    // crear carpeta local ./var/colmena/
    mkdir_if_needed("./var");
    mkdir_if_needed("./var/colmena");

    // init externs (asegura valores)
    head = NULL;
    list_lock = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;

    // crear colmena inicial
    colmena_t *c1 = crear_colmena(1);
    iniciar_colmena(c1);
    planificador_add_colmena(c1);

    // iniciar planificador y monitor
    planificador_start();
    monitor_start();

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigaction(SIGINT, &sa, NULL);

    // main queda esperando señales
    while (1) {
        pause();
    }

    return 0;
}
