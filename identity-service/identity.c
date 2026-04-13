#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define MAX_USERS 16
#define BUFFER_SIZE 256

//Base de datos de usuarios
typedef struct {
    char username[64];
    char password[64];
    char role[16];      //ATTACKER o DEFENDER
} User;

//Aquí viven los usuarios — solo en este servicio
static User users[] = {
    {"user1", "password123", "ATTACKER"},
    {"user2", "password123", "DEFENDER"},
    {"andres", "telematica", "ATTACKER"},
    {"sofia",  "telematica", "DEFENDER"},
};
static int user_count = 4;

//Buscar usuario
static const char *authenticate(const char *username, const char *password) {
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].username, username) == 0 &&
            strcmp(users[i].password, password) == 0) {
            return users[i].role;
        }
    }
    return NULL;
}

//Hilo por conexión
static void *handle_client(void *arg) {
    int fd = *(int *)arg;
    free(arg);

    char buffer[BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));

    ssize_t bytes = recv(fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes <= 0) { close(fd); return NULL; }

    buffer[strcspn(buffer, "\r\n")] = '\0';

    printf("[AUTH] Consulta: %s\n", buffer);

    //Parsear: AUTH username password
    char cmd[16], username[64], password[64];
    if (sscanf(buffer, "%15s %63s %63s", cmd, username, password) != 3 ||
        strcmp(cmd, "AUTH") != 0) {
        send(fd, "FAIL\r\n", 6, 0);
        printf("[AUTH] Formato inválido\n");
        close(fd);
        return NULL;
    }

    const char *role = authenticate(username, password);
    if (role) {
        char response[64];
        snprintf(response, sizeof(response), "OK %s\r\n", role);
        send(fd, response, strlen(response), 0);
        printf("[AUTH] %s → %s\n", username, role);
    } else {
        send(fd, "FAIL\r\n", 6, 0);
        printf("[AUTH] %s → FAIL\n", username);
    }

    close(fd);
    return NULL;
}


int main(int argc, char *argv[]) {
    int port = (argc >= 2) ? atoi(argv[1]) : 9090;

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket()"); return 1; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind()"); return 1;
    }
    if (listen(server_fd, 10) < 0) {
        perror("listen()"); return 1;
    }

    printf("[IDENTITY] Servicio de identidad en puerto %d\n", port);
    printf("[IDENTITY] %d usuarios registrados\n", user_count);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        int client_fd = accept(server_fd,
                               (struct sockaddr *)&client_addr, &len);
        if (client_fd < 0) { perror("accept()"); continue; }

        int *fd_ptr = malloc(sizeof(int));
        *fd_ptr = client_fd;
        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, fd_ptr);
        pthread_detach(tid);
    }

    close(server_fd);
    return 0;
}