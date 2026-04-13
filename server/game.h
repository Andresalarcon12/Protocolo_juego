#ifndef GAME_H
#define GAME_H

#include <pthread.h>

// ── Constantes del juego ──────────────────────────────
#define MAX_PLAYERS     16
#define MAX_ROOMS        8
#define MAX_RESOURCES    4
#define MAP_WIDTH      100
#define MAP_HEIGHT     100
// Distancia a la que el atacante "encuentra" un recurso
#define FIND_RADIUS      5

// ── Roles ─────────────────────────────────────────────
typedef enum {
    ROLE_NONE = 0,
    ROLE_ATTACKER,
    ROLE_DEFENDER
} Role;

// ── Estado de un recurso crítico ──────────────────────
typedef enum {
    RES_SAFE = 0,   // Sin ataque
    RES_UNDER_ATTACK,
    RES_BREACHED
} ResourceState;

typedef struct {
    int            id;
    int            x, y;
    ResourceState  state;
    int            breach_active;  // 1 si el timer está corriendo
} Resource;

// ── Estado de un jugador ──────────────────────────────
typedef struct {
    int   socket_fd;
    char  username[64];
    char  ip[46];
    int   port;
    Role  role;
    int   x, y;          // Posición en el plano
    int   room_id;        // -1 si está en lobby
    int   active;         // 1 si está conectado
} Player;

// ── Estado de una sala ────────────────────────────────
typedef struct {
    int       id;
    int       active;
    int       started;
    int       player_count;
    int       player_ids[MAX_PLAYERS]; // Índices en players[]
    Resource  resources[MAX_RESOURCES];
    int       resource_count;
} Room;

// ── Estado global del servidor ────────────────────────
typedef struct {
    Player       players[MAX_PLAYERS];
    int          player_count;
    Room         rooms[MAX_ROOMS];
    int          room_count;
    pthread_mutex_t lock;   // Mutex para acceso concurrente seguro
} GameState;

// ── API pública ───────────────────────────────────────
void  game_init(GameState *gs);
int   game_add_player(GameState *gs, int fd, const char *ip, int port);
void  game_remove_player(GameState *gs, int player_idx);
int   game_find_player_by_fd(GameState *gs, int fd);
int   game_create_room(GameState *gs);
int   game_join_room(GameState *gs, int player_idx, int room_id);
void  game_check_start(GameState *gs, int room_id);
// Broadcast: envía mensaje a todos los jugadores de una sala
void  game_broadcast(GameState *gs, int room_id, const char *msg, int exclude_fd);

// Arranca el timer de BREACH para un recurso
void game_start_breach_timer(GameState *gs, int room_idx, int resource_id);

#endif