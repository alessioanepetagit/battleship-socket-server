#include "game_manager.h"
#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>



void gm_init(GameManager *gm) {
    if (!gm) return;

    gm->player_count = 0;
    gm->game_count = 0;

    pthread_mutex_init(&gm->players_lock, NULL);
    pthread_mutex_init(&gm->games_lock, NULL);

    for (int i = 0; i < MAX_PLAYERS; i++) {
        gm->players[i].id = -1;
        gm->players[i].socket_fd = -1;
        gm->players[i].state = PLAYER_DISCONNECTED;
        gm->players[i].current_game_id = -1;
        gm->players[i].username[0] = '\0';
        pthread_mutex_init(&gm->players[i].lock, NULL);   /* MODIFICA */
    }

    for (int i = 0; i < MAX_GAMES; i++) {
        gm->games[i].id = -1;
        gm->games[i].state = GAME_FINISHED;
        pthread_mutex_init(&gm->games[i].lock, NULL);     /* MODIFICA */
    }
}

void gm_destroy(GameManager *gm) {
    if (!gm) return;

    /* MODIFICA: qui e solo qui distruggiamo i mutex degli slot */
    for (int i = 0; i < MAX_PLAYERS; i++) {
        pthread_mutex_destroy(&gm->players[i].lock);
    }
    for (int i = 0; i < MAX_GAMES; i++) {
        pthread_mutex_destroy(&gm->games[i].lock);
    }

    pthread_mutex_destroy(&gm->players_lock);
    pthread_mutex_destroy(&gm->games_lock);
}

Player* gm_add_player(GameManager *gm, int socket_fd) {
    if (!gm || socket_fd < 0) return NULL;

    pthread_mutex_lock(&gm->players_lock);

    int free_index = -1;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (gm->players[i].id == -1) {
            free_index = i;
            break;
        }
    }

    if (free_index == -1) {
        pthread_mutex_unlock(&gm->players_lock);
        return NULL;
    }

    Player *p = &gm->players[free_index];
    init_player(p, free_index + 1, socket_fd);   /* non tocca il mutex */
    gm->player_count++;

    pthread_mutex_unlock(&gm->players_lock);
    return p;
}

void gm_remove_player(GameManager *gm, int player_id) {
    if (!gm || player_id <= 0) return;

    pthread_mutex_lock(&gm->players_lock);
    int index = player_id - 1;
    if (index >= 0 && index < MAX_PLAYERS && gm->players[index].id == player_id) {
        pthread_mutex_lock(&gm->players[index].lock);
        gm->players[index].id = -1;
        gm->players[index].socket_fd = -1;
        gm->players[index].state = PLAYER_DISCONNECTED;
        gm->players[index].current_game_id = -1;
        gm->players[index].username[0] = '\0';
        pthread_mutex_unlock(&gm->players[index].lock);
        /* MODIFICA: niente pthread_mutex_destroy qui */
        gm->player_count--;
    }
    pthread_mutex_unlock(&gm->players_lock);
}

Player* gm_get_player(GameManager *gm, int player_id) {
    if (!gm || player_id <= 0) return NULL;

    pthread_mutex_lock(&gm->players_lock);
    int index = player_id - 1;
    Player *p = NULL;
    if (index >= 0 && index < MAX_PLAYERS && gm->players[index].id == player_id) {
        p = &gm->players[index];
    }
    pthread_mutex_unlock(&gm->players_lock);
    return p;
}

Player* gm_get_player_by_socket(GameManager *gm, int socket_fd) {
    if (!gm || socket_fd < 0) return NULL;

    pthread_mutex_lock(&gm->players_lock);
    Player *found = NULL;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (gm->players[i].id != -1 && gm->players[i].socket_fd == socket_fd) {
            found = &gm->players[i];
            break;
        }
    }
    pthread_mutex_unlock(&gm->players_lock);
    return found;
}

Player* gm_get_player_by_username(GameManager *gm, const char *username) {
    if (!gm || !username) return NULL;

    pthread_mutex_lock(&gm->players_lock);
    Player *found = NULL;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (gm->players[i].id != -1 && strcmp(gm->players[i].username, username) == 0) {
            found = &gm->players[i];
            break;
        }
    }
    pthread_mutex_unlock(&gm->players_lock);
    return found;
}


bool gm_set_username(GameManager *gm, int player_id, const char *username) {
    if (!gm || player_id <= 0 || !username || strlen(username) == 0) return false;

    int index = player_id - 1;

    pthread_mutex_lock(&gm->players_lock);

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (gm->players[i].id != -1 && strcmp(gm->players[i].username, username) == 0) {
            pthread_mutex_unlock(&gm->players_lock);
            return false;
        }
    }

    if (index < 0 || index >= MAX_PLAYERS || gm->players[index].id != player_id) {
        pthread_mutex_unlock(&gm->players_lock);
        return false;
    }

    strncpy(gm->players[index].username, username, MAX_USERNAME - 1);
    gm->players[index].username[MAX_USERNAME - 1] = '\0';

    pthread_mutex_unlock(&gm->players_lock);
    return true;
}

void gm_copy_username(Player *player, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!player) return;

    pthread_mutex_lock(&player->lock);
    strncpy(out, player->username[0] ? player->username : "Anonimo", out_size - 1);
    out[out_size - 1] = '\0';
    pthread_mutex_unlock(&player->lock);
}

Game* gm_create_game(GameManager *gm, int creator_id) {
    if (!gm || creator_id <= 0) return NULL;

    pthread_mutex_lock(&gm->games_lock);

    int free_index = -1;
    for (int i = 0; i < MAX_GAMES; i++) {
        if (gm->games[i].id == -1) {
            free_index = i;
            break;
        }
    }

    if (free_index == -1) {
        pthread_mutex_unlock(&gm->games_lock);
        return NULL;
    }

    Game *g = &gm->games[free_index];
    pthread_mutex_lock(&g->lock);
    init_game(g, free_index + 1, creator_id);   /* non tocca il mutex */
    pthread_mutex_unlock(&g->lock);
    gm->game_count++;

    pthread_mutex_unlock(&gm->games_lock);
    return g;
}

Game* gm_get_game(GameManager *gm, int game_id) {
    if (!gm || game_id <= 0) return NULL;

    pthread_mutex_lock(&gm->games_lock);
    int index = game_id - 1;
    Game *g = NULL;
    if (index >= 0 && index < MAX_GAMES && gm->games[index].id == game_id) {
        g = &gm->games[index];
    }
    pthread_mutex_unlock(&gm->games_lock);
    return g;
}

Game* gm_get_game_by_code(GameManager *gm, const char *code) {
    if (!gm || !code) return NULL;

    pthread_mutex_lock(&gm->games_lock);
    Game *found = NULL;
    for (int i = 0; i < MAX_GAMES; i++) {
        if (gm->games[i].id != -1 && strcmp(gm->games[i].game_code, code) == 0) {
            found = &gm->games[i];
            break;
        }
    }
    pthread_mutex_unlock(&gm->games_lock);
    return found;
}

void gm_remove_game(GameManager *gm, int game_id) {
    if (!gm || game_id <= 0) return;

    pthread_mutex_lock(&gm->games_lock);
    int index = game_id - 1;
    if (index >= 0 && index < MAX_GAMES && gm->games[index].id == game_id) {
        pthread_mutex_lock(&gm->games[index].lock);
        gm->games[index].id = -1;
        gm->games[index].state = GAME_FINISHED;
        gm->games[index].creator_id = -1;
        gm->games[index].opponent_id = -1;
        gm->games[index].pending_invite_from = -1;
        gm->games[index].game_code[0] = '\0';
        pthread_mutex_unlock(&gm->games[index].lock);
        gm->game_count--;
        printf("[MANAGER] Partita %d rimossa, slot liberato\n", game_id);
    }
    pthread_mutex_unlock(&gm->games_lock);
}

int gm_get_available_games(GameManager *gm, GameInfo *out_games, int max_results) {
    if (!gm || !out_games || max_results <= 0) return 0;

    pthread_mutex_lock(&gm->games_lock);
    int count = 0;
    for (int i = 0; i < MAX_GAMES && count < max_results; i++) {
        if (gm->games[i].id != -1 && gm->games[i].state == GAME_WAITING_PLAYERS) {
            out_games[count].id = gm->games[i].id;
            strncpy(out_games[count].game_code, gm->games[i].game_code, GAME_CODE_LEN - 1);
            out_games[count].game_code[GAME_CODE_LEN - 1] = '\0';
            out_games[count].creator_id = gm->games[i].creator_id;
            count++;
        }
    }
    pthread_mutex_unlock(&gm->games_lock);
    return count;
}

int gm_notify_player(Player *player, const char *message) {
    if (!player || !message) return -1;

    pthread_mutex_lock(&player->lock);
    int fd = player->socket_fd;
    pthread_mutex_unlock(&player->lock);

    if (fd < 0) return -1;
    return send_message(fd, message);
}
