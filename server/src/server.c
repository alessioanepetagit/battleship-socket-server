#include "server.h"
#include "client_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static Server *global_server = NULL;

static void handle_signal(int sig) {
    (void)sig;
    printf("\n[SERVER] Segnale di arresto ricevuto. Chiusura in corso...\n");
    if (global_server) {
        server_stop(global_server);
    }
}

static void setup_signals(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Ignora SIGPIPE: le scritture su socket chiusi restituiranno -1 con EPIPE */
    signal(SIGPIPE, SIG_IGN);
}

bool server_init(Server *server, int port) {
    if (!server) return false;

    server->port = (port > 0) ? port : DEFAULT_PORT;
    server->running = false;
    server->server_socket = -1;
    gm_init(&server->gm);

    global_server = server;
    setup_signals();

    server->server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server->server_socket < 0) {
        perror("[SERVER] Errore creazione socket");
        return false;
    }

    int opt = 1;
    if (setsockopt(server->server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("[SERVER] Errore setsockopt SO_REUSEADDR");
        close(server->server_socket);
        return false;
    }

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(server->port);

    if (bind(server->server_socket, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("[SERVER] Errore bind socket");
        close(server->server_socket);
        return false;
    }

    if (listen(server->server_socket, BACKLOG) < 0) {
        perror("[SERVER] Errore listen socket");
        close(server->server_socket);
        return false;
    }

    return true;
}

void server_start(Server *server) {
    if (!server || server->server_socket < 0) return;

    server->running = true;
    printf("\n---------------------------------------------------\n");
    printf("     SFIDA NAVALE - SERVER MULTIPLAYER\n");
    printf("---------------------------------------------------\n");
    printf("[SERVER] In ascolto sulla porta %d\n", server->port);
    printf("[SERVER] Pronto a ricevere connessioni...\n\n");

    while (server->running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = accept(server->server_socket, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (errno == EINTR) {
                /* Chiamata interrotta dal signal handler durante lo shutdown */
                continue;
            }
            if (!server->running) break;
            perror("[SERVER] Errore accept");
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        printf("[SERVER] Nuova connessione da %s:%d (FD %d)\n", client_ip, ntohs(client_addr.sin_port), client_fd);

        ThreadArgs *args = malloc(sizeof(ThreadArgs));
        if (!args) {
            perror("[SERVER] Errore malloc ThreadArgs");
            close(client_fd);
            continue;
        }
        args->socket_fd = client_fd;
        args->gm = &server->gm;

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, client_handler_thread, args) != 0) {
            perror("[SERVER] Errore creazione thread worker");
            free(args);
            close(client_fd);
            continue;
        }

        pthread_detach(thread_id);
    }
}

void server_stop(Server *server) {
    if (!server) return;

    server->running = false;
    if (server->server_socket >= 0) {
        close(server->server_socket);
        server->server_socket = -1;
    }
    gm_destroy(&server->gm);
    printf("[SERVER] Risorse deallocate. Server terminato.\n");
}
