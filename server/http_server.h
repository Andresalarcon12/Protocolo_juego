#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "game.h"

//Arranca el servidor HTTP en el puerto indicado
//Corre en un hilo separado para no bloquear el servidor de juego
void http_server_start(int port, GameState *gs);

#endif