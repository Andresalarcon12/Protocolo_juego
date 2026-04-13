#define _POSIX_C_SOURCE 200112L
#include "auth_client.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>

#define BUF_SIZE 256

int auth_request(const char *host, const char *port,
                 const char *username, const char *password,
                 char *role_out) {

    struct addrinfo hints, *res, *rp;
    int sock = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;

    if (getaddrinfo(host, port, &hints, &res) != 0)
        return -1;

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock < 0) continue;
        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(sock);
        sock = -1;
    }
    freeaddrinfo(res);

    if (sock < 0) return -1;

    char request[BUF_SIZE];
    snprintf(request, sizeof(request), "AUTH %s %s\r\n", username, password);
    if (send(sock, request, strlen(request), 0) < 0) {
        close(sock);
        return -1;
    }

    char response[BUF_SIZE];
    int bytes = recv(sock, response, sizeof(response) - 1, 0);
    if (bytes <= 0) {
        close(sock);
        return -1;
    }
    response[bytes] = '\0';
    response[strcspn(response, "\r\n")] = '\0';

    close(sock);

    // Parsear respuesta
    if (strncmp(response, "OK", 2) == 0) {
        if (sscanf(response, "OK %15s", role_out) != 1) {
            return -1;
        }
        return 1;
    }

    return 0;
}