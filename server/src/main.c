#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);  // Disabilita il buffering di stdout
    int port = DEFAULT_PORT;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) {
            if (i + 1 < argc) {
                port = atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Uso: %s [-p porta]\n", argv[0]);
            return 0;
        }
    }

    Server server;
    if (!server_init(&server, port)) {
        fprintf(stderr, "[FATAL] Inizializzazione server fallita.\n");
        return EXIT_FAILURE;
    }

    server_start(&server);
    return EXIT_SUCCESS;
}
