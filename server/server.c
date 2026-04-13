#include "server.h"
#include "logger.h"
#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <math.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define BUFFER_SIZE 512

static GameState gs;

static void send_msg(Player *p, const char *msg) {
    send(p->socket_fd, msg, strlen(msg), 0);
    logger_log(p->ip, p->port, "RES", msg);
}

static void check_found_resources(Player *p, int room_idx) {
    Room *r = &gs.rooms[room_idx];
    for (int i = 0; i < r->resource_count; i++) {
        Resource *res = &r->resources[i];
        if (res->state != RES_SAFE) continue;
        int dx = p->x - res->x;
        int dy = p->y - res->y;
        double dist = sqrt(dx*dx + dy*dy);
        if (dist <= FIND_RADIUS) {
            char msg[64];
            snprintf(msg, sizeof(msg), "FOUND %d %d %d\r\n",
                     res->id, res->x, res->y);
            send_msg(p, msg);
        }
    }
}

static Role query_identity_service(const char *username, const char *password) {
    struct addrinfo hints, *res, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int rc = getaddrinfo("localhost", "9090", &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "[AUTH] getaddrinfo falló: %s\n", gai_strerror(rc));
        return ROLE_NONE;
    }

    int fd = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd < 0) {
        fprintf(stderr, "[AUTH] No se pudo conectar al servicio de identidad\n");
        return ROLE_NONE;
    }

    char query[256];
    snprintf(query, sizeof(query), "AUTH %s %s\r\n", username, password);
    send(fd, query, strlen(query), 0);

    char response[64];
    memset(response, 0, sizeof(response));
    recv(fd, response, sizeof(response) - 1, 0);
    close(fd);

    response[strcspn(response, "\r\n")] = '\0';

    if (strncmp(response, "OK ", 3) == 0) {
        char role_str[16];
        sscanf(response + 3, "%15s", role_str);
        if (strcmp(role_str, "ATTACKER") == 0) return ROLE_ATTACKER;
        if (strcmp(role_str, "DEFENDER") == 0) return ROLE_DEFENDER;
    }
    return ROLE_NONE;
}

static void handle_hello(Player *p, char *params) {
    char username[64], password[64];
    if (sscanf(params, "%63s %63s", username, password) != 2) {
        send_msg(p, "ERR 422 INVALID_COMMAND\r\n");
        return;
    }
    if (p->role != ROLE_NONE) {
        send_msg(p, "ERR 409 ALREADY_LOGGED_IN\r\n");
        return;
    }

    Role assigned = query_identity_service(username, password);

    if (assigned == ROLE_NONE) {
        send_msg(p, "ERR 401 UNAUTHORIZED\r\n");
        return;
    }

    strncpy(p->username, username, sizeof(p->username));
    p->role = assigned;

    char resp[128];
    snprintf(resp, sizeof(resp), "WELCOME %s %s\r\n",
             username,
             assigned == ROLE_ATTACKER ? "ATTACKER" : "DEFENDER");
    send_msg(p, resp);
}

static void handle_list(Player *p) {
    if (p->role == ROLE_NONE) { send_msg(p, "ERR 401 UNAUTHORIZED\r\n"); return; }

    int count = 0;
    char body[512] = "";
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (gs.rooms[i].active) {
            char entry[64];
            snprintf(entry, sizeof(entry), " %d %d %s",
                     gs.rooms[i].id,
                     gs.rooms[i].player_count,
                     gs.rooms[i].started ? "STARTED" : "WAITING");
            strncat(body, entry, sizeof(body) - strlen(body) - 1);
            count++;
        }
    }
    char resp[600];
    snprintf(resp, sizeof(resp), "GAMES %d%s\r\n", count, body);
    send_msg(p, resp);
}

static void handle_create(Player *p) {
    if (p->role == ROLE_NONE) { send_msg(p, "ERR 401 UNAUTHORIZED\r\n"); return; }
    if (p->room_id >= 0)      { send_msg(p, "ERR 409 ALREADY_IN_GAME\r\n"); return; }

    int room_id = game_create_room(&gs);
    if (room_id < 0) { send_msg(p, "ERR 500 SERVER_ERROR\r\n"); return; }

    int pidx = game_find_player_by_fd(&gs, p->socket_fd);
    game_join_room(&gs, pidx, room_id);

    char resp[64];
    snprintf(resp, sizeof(resp), "JOINED %d %d %d\r\n",
             room_id, MAP_WIDTH, MAP_HEIGHT);
    send_msg(p, resp);
    game_check_start(&gs, room_id - 1);
}

static void handle_join(Player *p, char *params) {
    if (p->role == ROLE_NONE) { send_msg(p, "ERR 401 UNAUTHORIZED\r\n"); return; }
    if (p->room_id >= 0)      { send_msg(p, "ERR 409 ALREADY_IN_GAME\r\n"); return; }

    int room_id;
    if (sscanf(params, "%d", &room_id) != 1) {
        send_msg(p, "ERR 422 INVALID_COMMAND\r\n"); return;
    }

    int pidx = game_find_player_by_fd(&gs, p->socket_fd);
    if (game_join_room(&gs, pidx, room_id) < 0) {
        send_msg(p, "ERR 404 ROOM_NOT_FOUND\r\n"); return;
    }

    char resp[64];
    snprintf(resp, sizeof(resp), "JOINED %d %d %d\r\n",
             room_id, MAP_WIDTH, MAP_HEIGHT);
    send_msg(p, resp);
    game_check_start(&gs, room_id - 1);
}

static void handle_move(Player *p, char *params) {
    if (p->role == ROLE_NONE) { send_msg(p, "ERR 401 UNAUTHORIZED\r\n"); return; }
    if (p->room_id < 0)       { send_msg(p, "ERR 403 FORBIDDEN\r\n"); return; }

    int x, y;
    if (sscanf(params, "%d %d", &x, &y) != 2) {
        send_msg(p, "ERR 422 INVALID_COMMAND\r\n"); return;
    }
    if (x < 0 || x >= MAP_WIDTH || y < 0 || y >= MAP_HEIGHT) {
        send_msg(p, "ERR 422 OUT_OF_BOUNDS\r\n"); return;
    }

    p->x = x; p->y = y;

    char moved[128];
    snprintf(moved, sizeof(moved), "MOVED %s %d %d\r\n", p->username, x, y);
    game_broadcast(&gs, p->room_id, moved, -1);

    if (p->role == ROLE_ATTACKER)
        check_found_resources(p, p->room_id);
}

static void handle_attack(Player *p, char *params) {
    if (p->role != ROLE_ATTACKER) { send_msg(p, "ERR 403 FORBIDDEN\r\n"); return; }
    if (p->room_id < 0)           { send_msg(p, "ERR 403 FORBIDDEN\r\n"); return; }

    int res_id;
    if (sscanf(params, "%d", &res_id) != 1) {
        send_msg(p, "ERR 422 INVALID_COMMAND\r\n"); return;
    }

    Room *r = &gs.rooms[p->room_id];
    Resource *target = NULL;
    for (int i = 0; i < r->resource_count; i++) {
        if (r->resources[i].id == res_id) { target = &r->resources[i]; break; }
    }
    if (!target || target->state != RES_SAFE) {
        send_msg(p, "ERR 404 RESOURCE_NOT_FOUND\r\n"); return;
    }

    target->state = RES_UNDER_ATTACK;
    target->breach_active = 1;

    char alert[128];
    snprintf(alert, sizeof(alert), "ALERT %d %d %d 30\r\n",
             target->id, target->x, target->y);
    game_broadcast(&gs, p->room_id, alert, -1);
    //Arrancar el timer de BREACH
    game_start_breach_timer(&gs, p->room_id, res_id);
}

static void handle_defend(Player *p, char *params) {
    if (p->role != ROLE_DEFENDER) { send_msg(p, "ERR 403 FORBIDDEN\r\n"); return; }
    if (p->room_id < 0)           { send_msg(p, "ERR 403 FORBIDDEN\r\n"); return; }

    int res_id;
    if (sscanf(params, "%d", &res_id) != 1) {
        send_msg(p, "ERR 422 INVALID_COMMAND\r\n"); return;
    }

    Room *r = &gs.rooms[p->room_id];
    Resource *target = NULL;
    for (int i = 0; i < r->resource_count; i++) {
        if (r->resources[i].id == res_id) { target = &r->resources[i]; break; }
    }
    if (!target || target->state != RES_UNDER_ATTACK) {
        send_msg(p, "ERR 404 RESOURCE_NOT_FOUND\r\n"); return;
    }

    target->state = RES_SAFE;
    target->breach_active = 0;

    char msg[128];
    snprintf(msg, sizeof(msg), "MITIGATED %d %s\r\n", res_id, p->username);
    game_broadcast(&gs, p->room_id, msg, -1);
}

static void parse_command(Player *p, char *raw) {
    char cmd[32] = "";
    char params[BUFFER_SIZE] = "";

    raw[strcspn(raw, "\r\n")] = '\0';

    int parsed = sscanf(raw, "%31s %[^\n]", cmd, params);
    if (parsed < 1) { send_msg(p, "ERR 422 INVALID_COMMAND\r\n"); return; }

    if      (strcmp(cmd, "HELLO")  == 0) handle_hello(p, params);
    else if (strcmp(cmd, "LIST")   == 0) handle_list(p);
    else if (strcmp(cmd, "CREATE") == 0) handle_create(p);
    else if (strcmp(cmd, "JOIN")   == 0) handle_join(p, params);
    else if (strcmp(cmd, "MOVE")   == 0) handle_move(p, params);
    else if (strcmp(cmd, "ATTACK") == 0) handle_attack(p, params);
    else if (strcmp(cmd, "DEFEND") == 0) handle_defend(p, params);
    else if (strcmp(cmd, "QUIT")   == 0) { send_msg(p, "BYE\r\n"); }
    else send_msg(p, "ERR 422 INVALID_COMMAND\r\n");
}

static void *handle_client(void *arg) {
    ClientInfo *info = (ClientInfo *)arg;
    int fd   = info->socket_fd;
    char ip[46];
    int port = info->port;
    strncpy(ip, info->ip, sizeof(ip));
    free(info);

    pthread_mutex_lock(&gs.lock);
    int pidx = game_add_player(&gs, fd, ip, port);
    pthread_mutex_unlock(&gs.lock);

    if (pidx < 0) {
        const char *full = "ERR 500 SERVER_FULL\r\n";
        send(fd, full, strlen(full), 0);
        close(fd);
        return NULL;
    }

    logger_log(ip, port, "CON", "Cliente conectado");

    char buffer[BUFFER_SIZE];
    while (1) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes = recv(fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) {
            logger_log(ip, port, "DIS", "Cliente desconectado");
            break;
        }
        logger_log(ip, port, "REQ", buffer);

        pthread_mutex_lock(&gs.lock);
        Player *p = &gs.players[pidx];
        parse_command(p, buffer);
        pthread_mutex_unlock(&gs.lock);
    }

    pthread_mutex_lock(&gs.lock);
    game_remove_player(&gs, pidx);
    pthread_mutex_unlock(&gs.lock);

    close(fd);
    return NULL;
}

void server_run(int port) {
    game_init(&gs);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket()"); exit(EXIT_FAILURE); }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind()"); close(server_fd); exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 10) < 0) {
        perror("listen()"); close(server_fd); exit(EXIT_FAILURE);
    }

    printf("[SERVER] Escuchando en puerto %d...\n", port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd,
                               (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) { perror("accept()"); continue; }

        ClientInfo *info = malloc(sizeof(ClientInfo));
        info->socket_fd  = client_fd;
        info->port       = ntohs(client_addr.sin_port);
        inet_ntop(AF_INET, &client_addr.sin_addr, info->ip, sizeof(info->ip));

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, info) != 0) {
            perror("pthread_create()"); free(info); close(client_fd); continue;
        }
        pthread_detach(tid);
    }
    close(server_fd);
}

GameState *server_get_gamestate(void) {
    return &gs;
}