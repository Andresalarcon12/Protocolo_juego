#include "game.h"
#include "logger.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>

//Inicializa todo el estado del juego a cero
void game_init(GameState *gs) {
    memset(gs, 0, sizeof(GameState));
    pthread_mutex_init(&gs->lock, NULL);
    for (int i = 0; i < MAX_PLAYERS; i++) gs->players[i].room_id = -1;
    for (int i = 0; i < MAX_ROOMS;   i++) gs->rooms[i].id = i + 1;
}

//Registra un nuevo jugador conectado. Retorna su índice o -1 si no hay espacio.
int game_add_player(GameState *gs, int fd, const char *ip, int port) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!gs->players[i].active) {
            gs->players[i].socket_fd = fd;
            gs->players[i].active    = 1;
            gs->players[i].room_id   = -1;
            gs->players[i].x         = 0;
            gs->players[i].y         = 0;
            gs->players[i].role      = ROLE_NONE;
            strncpy(gs->players[i].ip, ip, sizeof(gs->players[i].ip));
            gs->players[i].port = port;
            gs->player_count++;
            return i;
        }
    }
    return -1; //Servidor lleno
}

//Marca al jugador como inactivo y lo saca de su sala
void game_remove_player(GameState *gs, int idx) {
    if (idx < 0 || idx >= MAX_PLAYERS) return;
    int room_id = gs->players[idx].room_id;
    if (room_id >= 0) {
        Room *r = &gs->rooms[room_id];
        for (int i = 0; i < r->player_count; i++) {
            if (r->player_ids[i] == idx) {
                //Desplazar el array para tapar el hueco
                r->player_ids[i] = r->player_ids[r->player_count - 1];
                r->player_count--;
                break;
            }
        }
    }
    memset(&gs->players[idx], 0, sizeof(Player));
    gs->players[idx].room_id = -1;
    gs->player_count--;
}

//Busca un jugador por su socket fd. Retorna índice o -1.
int game_find_player_by_fd(GameState *gs, int fd) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (gs->players[i].active && gs->players[i].socket_fd == fd)
            return i;
    }
    return -1;
}

//Crea una sala nueva. Retorna su room_id o -1 si no hay espacio.
int game_create_room(GameState *gs) {
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (!gs->rooms[i].active) {
            gs->rooms[i].active       = 1;
            gs->rooms[i].started      = 0;
            gs->rooms[i].player_count = 0;

            //Generar 2 recursos críticos en posiciones fijas
            gs->rooms[i].resource_count = 2;
            gs->rooms[i].resources[0] = (Resource){1, 25, 40, RES_SAFE, 0};
            gs->rooms[i].resources[1] = (Resource){2, 75, 70, RES_SAFE, 0};

            return gs->rooms[i].id; //id = i+1
        }
    }
    return -1;
}

//Une un jugador a una sala. Retorna 0 si ok, -1 si error.
int game_join_room(GameState *gs, int player_idx, int room_id) {
    //room_id es 1-based, índice es room_id-1
    int idx = room_id - 1;
    if (idx < 0 || idx >= MAX_ROOMS) return -1;
    Room *r = &gs->rooms[idx];
    if (!r->active)             return -1;
    if (r->player_count >= MAX_PLAYERS) return -1;

    r->player_ids[r->player_count++] = player_idx;
    gs->players[player_idx].room_id  = idx; //guardamos el índice
    return 0;
}

//Revisa si la sala tiene al menos 1 atacante y 1 defensor para arrancar
void game_check_start(GameState *gs, int room_idx) {
    Room *r = &gs->rooms[room_idx];
    if (r->started) return;

    int has_attacker = 0, has_defender = 0;
    for (int i = 0; i < r->player_count; i++) {
        Role role = gs->players[r->player_ids[i]].role;
        if (role == ROLE_ATTACKER) has_attacker = 1;
        if (role == ROLE_DEFENDER) has_defender = 1;
    }

    if (has_attacker && has_defender) {
        r->started = 1;
        char msg[64];
        snprintf(msg, sizeof(msg), "START %d %d\r\n",
                 r->id, r->player_count);
        game_broadcast(gs, room_idx, msg, -1);
    }
}

//Envía un mensaje a todos los jugadores de una sala
//exclude_fd: socket a omitir (-1 para enviar a todos)
void game_broadcast(GameState *gs, int room_idx, const char *msg, int exclude_fd) {
    Room *r = &gs->rooms[room_idx];
    for (int i = 0; i < r->player_count; i++) {
        Player *p = &gs->players[r->player_ids[i]];
        if (p->active && p->socket_fd != exclude_fd) {
            send(p->socket_fd, msg, strlen(msg), 0);
            logger_log(p->ip, p->port, "RES", msg);
        }
    }
    
}

//Timer de BREACH
typedef struct {
    GameState *gs;
    int        room_idx;
    int        resource_id;
} BreachArgs;

static void *breach_timer_thread(void *arg) {
    BreachArgs *b = (BreachArgs *)arg;
    
    //Esperar 30 segundos
    sleep(30);

    pthread_mutex_lock(&b->gs->lock);

    //Verificar si el recurso sigue bajo ataque
    Room *r = &b->gs->rooms[b->room_idx];
    for (int i = 0; i < r->resource_count; i++) {
        Resource *res = &r->resources[i];
        if (res->id == b->resource_id &&
            res->state == RES_UNDER_ATTACK &&
            res->breach_active) {

            //Marcar como comprometido
            res->state        = RES_BREACHED;
            res->breach_active = 0;

            //Notificar a toda la sala
            char msg[64];
            snprintf(msg, sizeof(msg), "BREACH %d\r\n", res->id);
            game_broadcast(b->gs, b->room_idx, msg, -1);

            printf("[BREACH] Recurso %d comprometido en sala %d\n",
                   res->id, r->id);
        }
    }

    pthread_mutex_unlock(&b->gs->lock);
    free(b);
    return NULL;
}

void game_start_breach_timer(GameState *gs, int room_idx, int resource_id) {
    BreachArgs *b = malloc(sizeof(BreachArgs));
    b->gs          = gs;
    b->room_idx    = room_idx;
    b->resource_id = resource_id;

    pthread_t tid;
    pthread_create(&tid, NULL, breach_timer_thread, b);
    pthread_detach(tid);
}