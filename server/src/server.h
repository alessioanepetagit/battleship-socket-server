#ifndef SERVER_H
#define SERVER_H

#include "game_manager.h"
#include <stdbool.h>

#define DEFAULT_PORT    8080
#define BACKLOG         16

typedef struct {
    int server_socket;
    int port;
    volatile bool running;
    GameManager gm;
} Server;

bool server_init(Server *server, int port);
void server_start(Server *server);
void server_stop(Server *server);
#endif
