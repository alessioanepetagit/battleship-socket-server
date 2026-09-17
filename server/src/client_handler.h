#ifndef CLIENT_HANDLER_H
#define CLIENT_HANDLER_H

#include "game_manager.h"

typedef struct {
    int socket_fd;
    GameManager *gm;
} ThreadArgs;

void* client_handler_thread(void *arg);

#endif
