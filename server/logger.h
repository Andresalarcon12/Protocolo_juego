#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>

// Inicializa el logger. Abre el archivo de logs.
// Retorna 0 si éxito, -1 si falla.
int logger_init(const char *log_filename);

// Cierra el archivo de logs limpiamente.
void logger_close(void);

// Registra un mensaje con timestamp, IP y puerto del cliente.
// direction: "REQ" (petición entrante) o "RES" (respuesta saliente)
void logger_log(const char *client_ip, int client_port,
                const char *direction, const char *message);

#endif