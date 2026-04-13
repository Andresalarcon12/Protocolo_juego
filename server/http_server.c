#include "http_server.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define HTTP_BUFFER 4096

static GameState *game_state;

// ── HTML base con estilos ─────────────────────────────
static const char *HTML_HEADER =
    "<!DOCTYPE html><html lang='es'><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<title>CDSP — Cyber Defense</title>"
    "<style>"
    "body{font-family:monospace;background:#0d0d1a;color:#00ff88;"
    "display:flex;justify-content:center;align-items:center;"
    "min-height:100vh;margin:0;}"
    ".card{background:#16213e;padding:2rem;border-radius:8px;"
    "border:1px solid #0f3460;min-width:320px;}"
    "h1{color:#00ff88;text-align:center;margin-bottom:1.5rem;}"
    "h2{color:#4444ff;margin-bottom:1rem;}"
    "input{width:100%;padding:8px;margin:6px 0 14px;"
    "background:#0d0d1a;border:1px solid #0f3460;"
    "color:#00ff88;border-radius:4px;box-sizing:border-box;}"
    "button{width:100%;padding:10px;background:#0f3460;"
    "color:#00ff88;border:none;border-radius:4px;"
    "cursor:pointer;font-size:1rem;font-family:monospace;}"
    "button:hover{background:#1a4a8a;}"
    ".error{color:#ff4444;margin-bottom:1rem;text-align:center;}"
    ".success{color:#00ff88;margin-bottom:1rem;text-align:center;}"
    "table{width:100%;border-collapse:collapse;margin-top:1rem;}"
    "th{color:#4444ff;border-bottom:1px solid #0f3460;padding:8px;text-align:left;}"
    "td{padding:8px;border-bottom:1px solid #1a1a2e;}"
    "a{color:#00ff88;}"
    ".badge{padding:2px 8px;border-radius:4px;font-size:0.85em;}"
    ".waiting{background:#1a3a1a;color:#00ff88;}"
    ".started{background:#3a1a1a;color:#ff4444;}"
    "</style></head><body><div class='card'>";

static const char *HTML_FOOTER = "</div></body></html>";

// ── Enviar respuesta HTTP completa ────────────────────
static void send_response(int fd, int status_code, const char *status_text,
                           const char *content_type, const char *body) {
    char header[512];
    int body_len = strlen(body);
    snprintf(header, sizeof(header),
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: %s; charset=utf-8\r\n"
             "Content-Length: %d\r\n"
             "Connection: close\r\n"
             "\r\n",
             status_code, status_text, content_type, body_len);
    send(fd, header, strlen(header), 0);
    send(fd, body, body_len, 0);
}

// ── Página de login ───────────────────────────────────
static void send_login_page(int fd, const char *error_msg) {
    char body[HTTP_BUFFER];
    snprintf(body, sizeof(body),
             "%s"
             "<h1>⚔ CDSP Login</h1>"
             "%s"
             "<form method='POST' action='/login'>"
             "<label>Usuario</label>"
             "<input type='text' name='username' placeholder='user1' required>"
             "<label>Contraseña</label>"
             "<input type='password' name='password' placeholder='••••••' required>"
             "<button type='submit'>Conectar</button>"
             "</form>"
             "<p style='text-align:center;margin-top:1rem;'>"
             "<a href='/games'>Ver partidas activas</a></p>"
             "%s",
             HTML_HEADER,
             error_msg ? error_msg : "",
             HTML_FOOTER);
    send_response(fd, 200, "OK", "text/html", body);
}

// ── Página de partidas activas ────────────────────────
static void send_games_page(int fd) {
    char rows[2048] = "";

    pthread_mutex_lock(&game_state->lock);
    int found = 0;
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (!game_state->rooms[i].active) continue;
        found++;
        char row[256];
        snprintf(row, sizeof(row),
                 "<tr>"
                 "<td>%d</td>"
                 "<td>%d jugadores</td>"
                 "<td><span class='badge %s'>%s</span></td>"
                 "</tr>",
                 game_state->rooms[i].id,
                 game_state->rooms[i].player_count,
                 game_state->rooms[i].started ? "started" : "waiting",
                 game_state->rooms[i].started ? "En curso" : "Esperando");
        strncat(rows, row, sizeof(rows) - strlen(rows) - 1);
    }
    pthread_mutex_unlock(&game_state->lock);

    char body[HTTP_BUFFER];
    if (found == 0) {
        snprintf(body, sizeof(body),
                 "%s<h1>⚔ CDSP</h1>"
                 "<h2>Partidas activas</h2>"
                 "<p>No hay partidas activas en este momento.</p>"
                 "<p><a href='/'>← Volver al login</a></p>%s",
                 HTML_HEADER, HTML_FOOTER);
    } else {
        snprintf(body, sizeof(body),
                 "%s<h1>⚔ CDSP</h1>"
                 "<h2>Partidas activas</h2>"
                 "<table>"
                 "<tr><th>Sala</th><th>Jugadores</th><th>Estado</th></tr>"
                 "%s"
                 "</table>"
                 "<p style='margin-top:1rem'><a href='/'>← Volver al login</a></p>"
                 "%s",
                 HTML_HEADER, rows, HTML_FOOTER);
    }
    send_response(fd, 200, "OK", "text/html", body);
}

// ── Procesar POST /login ──────────────────────────────
static void handle_login_post(int fd, const char *body) {
    // Parsear username y password del body
    // Formato: username=user1&password=pass
    char username[64] = "";
    char password[64] = "";

    const char *u = strstr(body, "username=");
    const char *p = strstr(body, "password=");

    if (u) sscanf(u + 9, "%63[^&\r\n]", username);
    if (p) sscanf(p + 9, "%63[^&\r\n]", password);

    // Validar contra usuarios conocidos
    // (En req 2 esto consultará el servicio de identidad)
    int valid = 0;
    const char *role = "";
    if (strcmp(username, "user1") == 0) { valid = 1; role = "ATTACKER"; }
    if (strcmp(username, "user2") == 0) { valid = 1; role = "DEFENDER"; }

    if (!valid) {
        char err[128];
        snprintf(err, sizeof(err),
                 "<p class='error'>Usuario o contraseña incorrectos.</p>");
        send_login_page(fd, err);
        return;
    }

    // Login exitoso: redirigir a /games con mensaje
    char body_html[HTTP_BUFFER];
    snprintf(body_html, sizeof(body_html),
             "%s<h1>⚔ CDSP</h1>"
             "<p class='success'>✓ Bienvenido, %s (%s)</p>"
             "<p>Usa el cliente del juego para conectarte al servidor TCP.</p>"
             "<h2>Partidas disponibles</h2>"
             "<p><a href='/games'>Ver partidas activas →</a></p>"
             "%s",
             HTML_HEADER, username, role, HTML_FOOTER);

    // Respuesta 302 redirect + mostrar página
    char redirect[512];
    snprintf(redirect, sizeof(redirect),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/html; charset=utf-8\r\n"
             "Content-Length: %d\r\n"
             "Connection: close\r\n"
             "\r\n", (int)strlen(body_html));
    send(fd, redirect, strlen(redirect), 0);
    send(fd, body_html, strlen(body_html), 0);
}

// ── Página 404 ────────────────────────────────────────
static void send_404(int fd) {
    char body[512];
    snprintf(body, sizeof(body),
             "%s<h1>404</h1><p>Página no encontrada.</p>"
             "<p><a href='/'>← Inicio</a></p>%s",
             HTML_HEADER, HTML_FOOTER);
    send_response(fd, 404, "Not Found", "text/html", body);
}

// ── Parsear y despachar petición HTTP ─────────────────
static void handle_http_request(int fd, const char *client_ip, int client_port) {
    char buffer[HTTP_BUFFER];
    ssize_t bytes = recv(fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes <= 0) return;
    buffer[bytes] = '\0';

    // Extraer método y ruta de la primera línea
    // Formato: "GET /ruta HTTP/1.1"
    char method[8] = "", path[256] = "", version[16] = "";
    sscanf(buffer, "%7s %255s %15s", method, path, version);

    logger_log(client_ip, client_port, "HTTP",
               buffer[0] ? buffer : "(vacío)");

    // Despachar según método y ruta
    if (strcmp(method, "GET") == 0) {
        if (strcmp(path, "/") == 0 || strcmp(path, "/login") == 0)
            send_login_page(fd, NULL);
        else if (strcmp(path, "/games") == 0)
            send_games_page(fd);
        else
            send_404(fd);

    } else if (strcmp(method, "POST") == 0) {
        if (strcmp(path, "/login") == 0) {
            // El body del POST está después de los headers (\r\n\r\n)
            char *body_start = strstr(buffer, "\r\n\r\n");
            if (body_start) body_start += 4;
            else body_start = "";
            handle_login_post(fd, body_start);
        } else {
            send_404(fd);
        }

    } else {
        // Método no soportado
        send_response(fd, 400, "Bad Request", "text/plain", "Bad Request");
    }
}

// ── Hilo por conexión HTTP ────────────────────────────
typedef struct { int fd; char ip[46]; int port; } HttpClient;

static void *http_client_thread(void *arg) {
    HttpClient *c = (HttpClient *)arg;
    handle_http_request(c->fd, c->ip, c->port);
    close(c->fd);
    free(c);
    return NULL;
}

// ── Hilo principal del servidor HTTP ─────────────────
static void *http_server_thread(void *arg) {
    int port = *(int *)arg;
    free(arg);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("http socket()"); return NULL; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("http bind()"); close(server_fd); return NULL;
    }
    if (listen(server_fd, 10) < 0) {
        perror("http listen()"); close(server_fd); return NULL;
    }

    printf("[HTTP] Servidor web en puerto %d\n", port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        int client_fd = accept(server_fd,
                               (struct sockaddr *)&client_addr, &len);
        if (client_fd < 0) { perror("http accept()"); continue; }

        HttpClient *c = malloc(sizeof(HttpClient));
        c->fd   = client_fd;
        c->port = ntohs(client_addr.sin_port);
        inet_ntop(AF_INET, &client_addr.sin_addr, c->ip, sizeof(c->ip));

        pthread_t tid;
        pthread_create(&tid, NULL, http_client_thread, c);
        pthread_detach(tid);
    }

    close(server_fd);
    return NULL;
}

// ── Punto de entrada público ──────────────────────────
void http_server_start(int port, GameState *gs) {
    game_state = gs;
    int *p = malloc(sizeof(int));
    *p = port;
    pthread_t tid;
    pthread_create(&tid, NULL, http_server_thread, p);
    pthread_detach(tid);
}