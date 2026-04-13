#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define BUFFER_SIZE 512
#define MAP_W 100
#define MAP_H 100

//Estado del cliente
static char username[64] = "";
static char role[16]     = "";
static int  pos_x        = 0;
static int  pos_y        = 0;
static int  room_id      = -1;
static int  started      = 0;
static int  sock_fd      = -1;
static int  running      = 1;

//Colores ANSI para la consola
#define RESET   "\033[0m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define CYAN    "\033[36m"
#define BOLD    "\033[1m"

//Enviar comando al servidor
static void send_cmd(const char *cmd) {
    char buf[BUFFER_SIZE];
    snprintf(buf, sizeof(buf), "%s\r\n", cmd);
    if (send(sock_fd, buf, strlen(buf), 0) < 0) {
        perror("send");
    }
}

//Dibujar el minimapa en consola
//Muestra una vista 21x21 centrada en el jugador
static void draw_map(void) {
    printf("\n" BOLD "═══════════════════════════════════════\n");
    printf("  CDSP — %s (%s) | Sala: %d | Pos: (%d,%d)\n",
           username, role, room_id, pos_x, pos_y);
    printf("═══════════════════════════════════════\n" RESET);

    int view = 10; // radio de visión
    for (int dy = -view; dy <= view; dy++) {
        printf("  ");
        for (int dx = -view; dx <= view; dx++) {
            int wx = pos_x + dx;
            int wy = pos_y + dy;

            //Fuera del mapa
            if (wx < 0 || wx >= MAP_W || wy < 0 || wy >= MAP_H) {
                printf(BLUE "░" RESET);
                continue;
            }
            //Posición propia
            if (dx == 0 && dy == 0) {
                if (strcmp(role, "ATTACKER") == 0)
                    printf(GREEN BOLD "A" RESET);
                else
                    printf(BLUE BOLD "D" RESET);
                continue;
            }
            //Recursos críticos conocidos (hardcodeados para demo)
            if ((wx == 25 && wy == 40) || (wx == 75 && wy == 70)) {
                printf(RED "S" RESET);
                continue;
            }
            printf("·");
        }
        printf("\n");
    }
    printf(BOLD "═══════════════════════════════════════\n" RESET);
}

//Procesar mensajes del servidor
static void process_message(const char *msg) {
    char line[BUFFER_SIZE];
    strncpy(line, msg, sizeof(line) - 1);
    line[strcspn(line, "\r\n")] = '\0';

    char cmd[32] = "";
    char rest[BUFFER_SIZE] = "";
    sscanf(line, "%31s %[^\n]", cmd, rest);

    if (strcmp(cmd, "WELCOME") == 0) {
        sscanf(rest, "%63s %15s", username, role);
        printf(GREEN "\n[✓] Login exitoso: %s (%s)\n" RESET, username, role);

    } else if (strcmp(cmd, "JOINED") == 0) {
        sscanf(rest, "%d", &room_id);
        printf(CYAN "\n[✓] Unido a sala %d. Esperando jugadores...\n" RESET, room_id);

    } else if (strcmp(cmd, "START") == 0) {
        started = 1;
        printf(YELLOW "\n" BOLD "[!] ¡LA PARTIDA COMENZÓ!\n" RESET);
        draw_map();

    } else if (strcmp(cmd, "GAMES") == 0) {
        int count;
        sscanf(rest, "%d", &count);
        printf(CYAN "\n[LOBBY] Partidas activas: %d\n" RESET, count);
        //Parsear entradas: room_id players status
        char *token = strtok(rest, " ");
        token = strtok(NULL, " "); //saltar el count
        while (token != NULL) {
            char rid[16], players[16], status[32];
            strncpy(rid, token, sizeof(rid));
            token = strtok(NULL, " ");
            if (!token) break;
            strncpy(players, token, sizeof(players));
            token = strtok(NULL, " ");
            if (!token) break;
            strncpy(status, token, sizeof(status));
            printf("  → Sala %s | %s jugadores | %s\n", rid, players, status);
            token = strtok(NULL, " ");
        }

    } else if (strcmp(cmd, "MOVED") == 0) {
        char uname[64];
        int x, y;
        sscanf(rest, "%63s %d %d", uname, &x, &y);
        if (strcmp(uname, username) == 0) {
            pos_x = x; pos_y = y;
            draw_map();
        } else {
            printf(YELLOW "  [MOV] %s → (%d, %d)\n" RESET, uname, x, y);
        }

    } else if (strcmp(cmd, "FOUND") == 0) {
        int rid, rx, ry;
        sscanf(rest, "%d %d %d", &rid, &rx, &ry);
        printf(RED BOLD "\n[!] ¡RECURSO CRÍTICO %d ENCONTRADO en (%d,%d)!\n" RESET,
               rid, rx, ry);
        printf(YELLOW "    Usa: ATTACK %d\n" RESET, rid);

    } else if (strcmp(cmd, "ALERT") == 0) {
        int rid, rx, ry, tl;
        sscanf(rest, "%d %d %d %d", &rid, &rx, &ry, &tl);
        printf(RED BOLD "\n[ALERTA] ¡Recurso %d bajo ataque en (%d,%d)! Tienes %ds\n" RESET,
               rid, rx, ry, tl);
        printf(YELLOW "    Usa: DEFEND %d\n" RESET, rid);

    } else if (strcmp(cmd, "MITIGATED") == 0) {
        char rid[16], uname[64];
        sscanf(rest, "%s %s", rid, uname);
        printf(GREEN BOLD "\n[✓] Recurso %s mitigado por %s\n" RESET, rid, uname);

    } else if (strcmp(cmd, "BREACH") == 0) {
        char rid[16];
        sscanf(rest, "%s", rid);
        printf(RED BOLD "\n[✗] ¡Recurso %s COMPROMETIDO! Tiempo agotado.\n" RESET, rid);

    } else if (strcmp(cmd, "GAMEOVER") == 0) {
        char winner[32];
        int sc_att, sc_def;
        sscanf(rest, "%31s %d %d", winner, &sc_att, &sc_def);
        printf(BOLD "\n╔══════════════════════════╗\n");
        printf("║      FIN DE PARTIDA      ║\n");
        printf("║  Ganador: %-14s║\n", winner);
        printf("║  Atacante: %d pts         ║\n", sc_att);
        printf("║  Defensor: %d pts         ║\n", sc_def);
        printf("╚══════════════════════════╝\n" RESET);
        running = 0;

    } else if (strcmp(cmd, "ERR") == 0) {
        printf(RED "\n[ERR] %s\n" RESET, rest);

    } else if (strcmp(cmd, "BYE") == 0) {
        printf(CYAN "\n[INFO] Desconectado.\n" RESET);
        running = 0;

    } else {
        printf(CYAN "\n← %s\n" RESET, line);
    }
}

//Hilo receptor
static void *receiver(void *arg) {
    (void)arg;
    char buffer[BUFFER_SIZE * 4];
    char leftover[BUFFER_SIZE * 4] = "";

    while (running) {
        char tmp[BUFFER_SIZE];
        ssize_t bytes = recv(sock_fd, tmp, sizeof(tmp) - 1, 0);
        if (bytes <= 0) {
            printf(RED "\n[INFO] Conexión cerrada.\n" RESET);
            running = 0;
            break;
        }
        tmp[bytes] = '\0';

        //Acumular en buffer con posible resto anterior
        strncat(leftover, tmp, sizeof(leftover) - strlen(leftover) - 1);

        //Procesar línea por línea
        char *ptr = leftover;
        char *end;
        while ((end = strstr(ptr, "\r\n")) != NULL) {
            *end = '\0';
            strncpy(buffer, ptr, sizeof(buffer) - 1);
            process_message(buffer);
            ptr = end + 2;
        }
        //Guardar lo que quedó sin \r\n
        memmove(leftover, ptr, strlen(ptr) + 1);
    }
    return NULL;
}

//Mostrar ayuda
static void print_help(void) {
    printf(CYAN "\nComandos disponibles:\n");
    printf("  HELLO <user> <pass>  — Iniciar sesión\n");
    printf("  LIST                 — Listar partidas\n");
    printf("  CREATE               — Crear sala\n");
    printf("  JOIN <room_id>       — Unirse a sala\n");
    printf("  MOVE <x> <y>         — Mover a coordenadas\n");
    printf("  w/a/s/d              — Mover un paso\n");
    printf("  ATTACK <resource_id> — Atacar recurso\n");
    printf("  DEFEND <resource_id> — Defender recurso\n");
    printf("  QUIT                 — Salir\n");
    printf("  help                 — Esta ayuda\n" RESET);
}


int main(int argc, char *argv[]) {
    //Recibe host y puerto por argumento, o usa defaults
    const char *host = (argc >= 2) ? argv[1] : "localhost";
    const char *port_str = (argc >= 3) ? argv[2] : "8080";

    printf(BOLD CYAN "╔══════════════════════════╗\n");
    printf("║     CDSP Client (C)      ║\n");
    printf("╚══════════════════════════╝\n" RESET);
    printf("Conectando a %s:%s...\n", host, port_str);

    //Resolución DNS con getaddrinfo (sin IPs hardcodeadas)
    struct addrinfo hints, *res, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;    //IPv4 o IPv6
    hints.ai_socktype = SOCK_STREAM;  //TCP

    int rc = getaddrinfo(host, port_str, &hints, &res);
    if (rc != 0) {
        //Manejo de excepción: fallo de resolución sin terminar el proceso
        fprintf(stderr, RED "[ERROR] getaddrinfo: %s\n" RESET, gai_strerror(rc));
        fprintf(stderr, "Verifica el nombre de host e intenta de nuevo.\n");
        return EXIT_FAILURE;
    }

    //Intentar conectar con cada resultado de DNS
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sock_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock_fd < 0) continue;
        if (connect(sock_fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(sock_fd);
        sock_fd = -1;
    }
    freeaddrinfo(res);

    if (sock_fd < 0) {
        fprintf(stderr, RED "[ERROR] No se pudo conectar a %s:%s\n" RESET,
                host, port_str);
        return EXIT_FAILURE;
    }

    printf(GREEN "[✓] Conectado. Escribe 'help' para ver los comandos.\n" RESET);

    //Arrancar hilo receptor
    pthread_t tid;
    pthread_create(&tid, NULL, receiver, NULL);
    pthread_detach(tid);

    //Bucle principal de entrada
    char input[BUFFER_SIZE];
    print_help();

    while (running) {
        printf(BOLD "> " RESET);
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL) break;
        input[strcspn(input, "\n")] = '\0';

        if (strlen(input) == 0) continue;

        //Atajos de movimiento con wasd
        if (started && strlen(input) == 1) {
            int nx = pos_x, ny = pos_y;
            switch (input[0]) {
                case 'w': ny = (pos_y > 0)          ? pos_y - 1 : pos_y; break;
                case 's': ny = (pos_y < MAP_H - 1)  ? pos_y + 1 : pos_y; break;
                case 'a': nx = (pos_x > 0)          ? pos_x - 1 : pos_x; break;
                case 'd': nx = (pos_x < MAP_W - 1)  ? pos_x + 1 : pos_x; break;
                default: printf(RED "[!] Comando no reconocido. Escribe 'help'.\n" RESET); continue;
            }
            char move_cmd[32];
            snprintf(move_cmd, sizeof(move_cmd), "MOVE %d %d", nx, ny);
            send_cmd(move_cmd);
            continue;
        }

        if (strcmp(input, "help") == 0) {
            print_help();
            continue;
        }

        if (strcmp(input, "QUIT") == 0 || strcmp(input, "quit") == 0) {
            send_cmd("QUIT");
            break;
        }

        //Cualquier otro comando se envía directo al servidor
        send_cmd(input);
    }

    running = 0;
    close(sock_fd);
    printf(CYAN "[INFO] Cliente cerrado.\n" RESET);
    return EXIT_SUCCESS;
}