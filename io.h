#ifndef IO_H
#define IO_H

#include <stdbool.h>

struct colmena;

// Inicializa el hilo de E/S
void io_init(void);

// Detiene el hilo de e/s y limpia la cola
void io_shutdown(void);

// Simula una operación de E/S bloqueante para una colmena
void io_solicitar(struct colmena *c, int dur_ms);

#endif 