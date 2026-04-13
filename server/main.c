#include <stdio.h>
#include <stdlib.h>
#include "logger.h"
#include "server.h"
#include "http_server.h"

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <puerto> <archivoDeLogs>\n", argv[0]);
        fprintf(stderr, "Ejemplo: %s 8080 servidor.log\n", argv[0]);
        return EXIT_FAILURE;
    }

    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Puerto inválido: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    if (logger_init(argv[2]) != 0) {
        fprintf(stderr, "No se pudo inicializar el logger.\n");
        return EXIT_FAILURE;
    }

    printf("[MAIN] Iniciando servidor CDSP en puerto %d\n", port);
    printf("[MAIN] Interfaz web en puerto %d\n", port + 1);

    //Arrancar servidor HTTP en puerto+1 (ej: 8081)
    http_server_start(port + 1, server_get_gamestate());

    //Arrancar servidor de juego (bloquea aquí)
    server_run(port);

    logger_close();
    return EXIT_SUCCESS;
}