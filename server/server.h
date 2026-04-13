#ifndef SERVER_H
#define SERVER_H

#include "game.h"

//Datos que se pasan a cada hilo de cliente
typedef struct {
    int socket_fd;       //El socket de esta conexión
    char ip[46];         //IP del cliente (funciona con IPv4 e IPv6)
    int port;            //Puerto origen del cliente
} ClientInfo;

//Arranca el servidor en el puerto indicado
//Bloquea indefinidamente aceptando clientes
void server_run(int port);

//Retorna el estado global del juego (para el servidor HTTP)
GameState *server_get_gamestate(void);
#endif