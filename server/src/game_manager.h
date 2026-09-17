#ifndef GAME_MANAGER_H
#define GAME_MANAGER_H

#include "game_logic.h"
#include <pthread.h>

#define MAX_PLAYERS 64
#define MAX_GAMES   32

/*
 * MODIFICA: per la lista delle partite disponibili non copiamo piu' l'intera
 * struct Game (che contiene un pthread_mutex_t: copiarlo e' undefined
 * behavior). Copiamo solo i campi che servono davvero al client.
 */
typedef struct {
    int id;
    char game_code[GAME_CODE_LEN];
    int creator_id;
} GameInfo;

typedef struct {
    Player players[MAX_PLAYERS];
    int player_count;
    pthread_mutex_t players_lock;

    Game games[MAX_GAMES];
    int game_count;
    pthread_mutex_t games_lock;
} GameManager;

void gm_init(GameManager *gm);
void gm_destroy(GameManager *gm);

/* Gestione Giocatori */
Player* gm_add_player(GameManager *gm, int socket_fd);
void gm_remove_player(GameManager *gm, int player_id);
Player* gm_get_player(GameManager *gm, int player_id);
Player* gm_get_player_by_socket(GameManager *gm, int socket_fd);
Player* gm_get_player_by_username(GameManager *gm, const char *username);
bool gm_set_username(GameManager *gm, int player_id, const char *username);
/* MODIFICA: copia thread-safe dello username (serve nei log e nelle notifiche) */
void gm_copy_username(Player *player, char *out, size_t out_size);

/* Gestione Partite */
Game* gm_create_game(GameManager *gm, int creator_id);
Game* gm_get_game(GameManager *gm, int game_id);
Game* gm_get_game_by_code(GameManager *gm, const char *code);
void gm_remove_game(GameManager *gm, int game_id);
int gm_get_available_games(GameManager *gm, GameInfo *out_games, int max_results);

/* Notifiche concorrenti thread-safe */
int gm_notify_player(Player *player, const char *message);

#endif
