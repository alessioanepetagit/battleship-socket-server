#ifndef CLIENT_H
#define CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <pthread.h>

#define DEFAULT_SERVER_HOST "127.0.0.1"
#define DEFAULT_SERVER_PORT 8080
#define MAX_MESSAGE_LEN     1024
#define MAX_USERNAME        32
#define GRID_SIZE           10

typedef enum {
    CLIENT_DISCONNECTED,
    CLIENT_CONNECTED,
    CLIENT_LOGGED_IN,
    CLIENT_IN_LOBBY,
    CLIENT_WAITING_OPPONENT,
    CLIENT_PLACING_SHIPS,
    CLIENT_MY_TURN,
    CLIENT_WAITING_TURN,
    CLIENT_OPPONENT_DISCONNECTED,
    CLIENT_GAME_OVER,
    CLIENT_WAITING_LOGIN,
    CLIENT_WAITING_START,
    /* MODIFICA: stato generico "ho mandato un comando, aspetto la risposta
       del server" (usato per LEAVE_GAME, REMATCH e REMATCH_DECLINE).
       Sostituisce il vecchio CLIENT_WAITING_REMATCH, che restava appeso
       per sempre perche' il server non gestiva la rivincita. */
    CLIENT_WAITING_SERVER
} ClientState;

typedef struct {
    int socket_fd;
    int player_id;
    char username[MAX_USERNAME];
    volatile ClientState state;
    char my_board[GRID_SIZE][GRID_SIZE];
    char enemy_board[GRID_SIZE][GRID_SIZE];
    int ships_placed;
    char current_game_code[8];
    bool opponent_wants_rematch;   /* MODIFICA: l'avversario ha gia' detto si' */
    pthread_mutex_t ui_lock;
    pthread_mutex_t send_lock; /* protegge le write() sul socket: il thread
                                  principale e il receiver thread possono
                                  entrambi inviare messaggi (es. READY
                                  automatico), senza questo lock i byte di
                                  due send() concorrenti potrebbero
                                  interfogliarsi sullo stesso stream TCP */
} Client;

void client_init(Client *client);
int client_connect(Client *client, const char *host, int port);
void client_disconnect(Client *client);
int client_send(Client *client, const char *message);
int client_receive(Client *client, char *buffer, size_t buf_size);
void client_run(Client *client);

#endif
