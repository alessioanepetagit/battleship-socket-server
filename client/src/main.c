/*
 * main.c - Entry point del client Battaglia Navale
 */

#include "client.h"
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#define VERSION "1.1.0"

static void handle_sigint(int sig) {
    (void)sig;
    g_sigint_received = 1;
}

static void print_usage(const char *prog) {
    printf("Uso: %s [opzioni]\n", prog);
    printf("\nOpzioni:\n");
    printf("  -h, --host HOST    Indirizzo o nome del server (default: %s)\n", DEFAULT_SERVER_HOST);
    printf("  -p, --port PORT    Porta del server (default: %d)\n", DEFAULT_SERVER_PORT);
    printf("  --help             Mostra questo messaggio\n");
    printf("\nVariabili d'ambiente (usate da docker-compose):\n");
    printf("  SERVER_HOST, SERVER_PORT\n");
    printf("\nEsempi:\n");
    printf("  %s                          Connetti a localhost:8080\n", prog);
    printf("  %s -h server -p 8080        Connetti al container 'server'\n", prog);
}

int main(int argc, char *argv[]) {
    const char *host = DEFAULT_SERVER_HOST;
    int port = DEFAULT_SERVER_PORT;

 
    const char *env_host = getenv("SERVER_HOST");
    const char *env_port = getenv("SERVER_PORT");
    if (env_host && strlen(env_host) > 0) host = env_host;
    if (env_port && atoi(env_port) > 0)   port = atoi(env_port);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--host") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Errore: -h richiede un indirizzo\n");
                return 1;
            }
            host = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Errore: -p richiede un numero di porta\n");
                return 1;
            }
            port = atoi(argv[++i]);
            if (port <= 0 || port > 65535) {
                fprintf(stderr, "Errore: porta non valida\n");
                return 1;
            }
        }
    }

    Client client;
    client_init(&client);
    signal(SIGINT, handle_sigint);

    if (client_connect(&client, host, port) < 0) {
        ui_show_error("Impossibile connettersi al server");
        printf("  Verifica che il server sia in esecuzione (%s:%d).\n\n", host, port);
        return 1;
    }
    client_run(&client);
    return 0;
}
