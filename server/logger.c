#include "logger.h"
#include <stdio.h>
#include <time.h>
#include <string.h>

// El archivo de logs se mantiene abierto durante toda la vida del servidor
static FILE *log_file = NULL;

int logger_init(const char *log_filename) {
    // "a" = append: no borra logs anteriores al reiniciar
    log_file = fopen(log_filename, "a");
    if (log_file == NULL) {
        perror("logger_init: no se pudo abrir el archivo de logs");
        return -1;
    }
    printf("[LOGGER] Logs guardándose en: %s\n", log_filename);
    return 0;
}

void logger_close(void) {
    if (log_file != NULL) {
        fclose(log_file);
        log_file = NULL;
    }
}

void logger_log(const char *client_ip, int client_port,
                const char *direction, const char *message) {
    // Construir timestamp legible: "2026-03-16 14:32:01"
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", t);

    // Eliminar \r\n del mensaje para que el log quede limpio
    char clean[512];
    strncpy(clean, message, sizeof(clean) - 1);
    clean[sizeof(clean) - 1] = '\0';
    // Reemplazar \r y \n por nada
    for (int i = 0; clean[i]; i++) {
        if (clean[i] == '\r' || clean[i] == '\n') clean[i] = ' ';
    }

    // Formato: [timestamp] [IP:puerto] [dirección] mensaje
    char line[640];
    snprintf(line, sizeof(line), "[%s] [%s:%d] [%s] %s\n",
             timestamp, client_ip, client_port, direction, clean);

    // Escribir en consola
    printf("%s", line);

    // Escribir en archivo
    if (log_file != NULL) {
        fprintf(log_file, "%s", line);
        fflush(log_file); // Forzar escritura inmediata (importante en crashes)
    }
}