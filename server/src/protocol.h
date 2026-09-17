#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>

#define PROTO_SEPARATOR     '|'
#define PROTO_TERMINATOR    '\n'
#define MAX_MESSAGE_LEN     1024
#define MAX_PARAMS          10
#define MAX_PARAM_LEN       256

/*
 * MODIFICA: ripulito l'elenco dei comandi.
 * - tolti END_GAME e WAIT_NEW_PLAYER (erano dichiarati ma mai gestiti)
 * - aggiunto LEAVE_GAME: esce dalla PARTITA ma resta connesso (torna in lobby)
 * - QUIT_GAME rinominato QUIT: chiude davvero la sessione
 * L'ordine di questo enum deve combaciare con COMMAND_STRINGS in protocol.c
 */
typedef enum {
    CMD_LOGIN,
    CMD_CREATE_GAME,
    CMD_LIST_GAMES,
    CMD_JOIN_GAME,
    CMD_ACCEPT_INVITE,
    CMD_REJECT_INVITE,
    CMD_PLACE_SHIP,
    CMD_READY,
    CMD_FIRE,
    CMD_LEAVE_GAME,
    CMD_REMATCH,
    CMD_REMATCH_DECLINE,
    CMD_QUIT,
    CMD_UNKNOWN
} CommandType;

typedef enum {
    RSP_OK,
    RSP_ERROR,
    RSP_WELCOME,
    RSP_GAME_CREATED,
    RSP_GAME_LIST,
    RSP_JOIN_REQUEST,
    RSP_JOIN_ACCEPTED,
    RSP_JOIN_REJECTED,
    RSP_SHIP_PLACED,
    RSP_INVALID_PLACEMENT,
    RSP_GAME_START,
    RSP_YOUR_TURN,
    RSP_WAIT_TURN,
    RSP_HIT,
    RSP_MISS,
    RSP_SUNK,
    RSP_ENEMY_FIRE,
    RSP_YOU_WIN,
    RSP_YOU_LOSE,
    RSP_OPPONENT_DISCONNECTED,
    RSP_PLAY_AGAIN_PROMPT,
    RSP_REMATCH_REQUEST,
    RSP_REMATCH_REJECTED,
    RSP_BACK_TO_LOBBY   /* MODIFICA: il server dice esplicitamente al client
                           "sei di nuovo in lobby, la partita non c'e' piu'" */
} ResponseType;

typedef enum {
    ERR_NONE = 0,
    ERR_INVALID_COMMAND = 100,
    ERR_INVALID_PARAMS = 101,
    ERR_USERNAME_TAKEN = 200,
    ERR_NOT_LOGGED_IN = 201,
    ERR_GAME_NOT_FOUND = 300,
    ERR_GAME_FULL = 301,
    ERR_NOT_YOUR_TURN = 302,
    ERR_INVALID_COORDS = 303,
    ERR_ALREADY_FIRED = 304,
    ERR_REMATCH_NOT_AVAILABLE = 305,
    ERR_NO_PENDING_INVITE = 306,   /* MODIFICA */
    ERR_CANNOT_JOIN_OWN_GAME = 307,/* MODIFICA */
    ERR_WRONG_STATE = 308,         /* MODIFICA */
    ERR_SHIP_OVERLAP = 400,
    ERR_SHIP_OUT_OF_BOUNDS = 401,
    ERR_SHIP_INVALID_SIZE = 402,
    ERR_ALL_SHIPS_PLACED = 403,
    ERR_SERVER_FULL = 500,
    ERR_INTERNAL = 501
} ErrorCode;

typedef struct {
    CommandType type;
    char params[MAX_PARAMS][MAX_PARAM_LEN];
    int param_count;
    char raw[MAX_MESSAGE_LEN];
} Message;

bool parse_message(const char *raw_message, Message *msg);
int build_response(ResponseType response, char *buffer, size_t buf_size, int param_count, ...);
int send_message(int socket_fd, const char *message);
int receive_message(int socket_fd, char *buffer, size_t buf_size);
const char* command_to_string(CommandType cmd);
const char* response_to_string(ResponseType rsp);
const char* error_to_string(ErrorCode err);

#endif
