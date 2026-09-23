#include "client_handler.h"
#include "protocol.h"
#include "game_logic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
 
/* ------------------------------------------------------------------ */
/* Funzioni di supporto                                               */
/* ------------------------------------------------------------------ */
 
/* scorciatoia per mandare un errore, cosi' non ripetiamo
   build_response + gm_notify_player dappertutto. */
static void send_error(Player *player, ErrorCode code) {
    char resp[MAX_MESSAGE_LEN];
    char code_str[16];
    snprintf(code_str, sizeof(code_str), "%d", (int)code);
    build_response(RSP_ERROR, resp, sizeof(resp), 2, code_str, error_to_string(code));
    gm_notify_player(player, resp);
}
 
static void send_simple(Player *player, ResponseType type) {
    char resp[MAX_MESSAGE_LEN];
    build_response(type, resp, sizeof(resp), 0);
    gm_notify_player(player, resp);
}
 
static void send_ok(Player *player, const char *text) {
    char resp[MAX_MESSAGE_LEN];
    build_response(RSP_OK, resp, sizeof(resp), 1, text);
    gm_notify_player(player, resp);
}
 
/* Lettura protetta dello stato del giocatore */
static PlayerState get_player_state(Player *player) {
    pthread_mutex_lock(&player->lock);
    PlayerState s = player->state;
    pthread_mutex_unlock(&player->lock);
    return s;
}
 
static int get_player_game_id(Player *player) {
    pthread_mutex_lock(&player->lock);
    int id = player->current_game_id;
    pthread_mutex_unlock(&player->lock);
    return id;
}
 
/*
 *  tutti i comandi tranne LOGIN e QUIT passano da qui.
 */
static bool require_login(Player *player) {
    if (get_player_state(player) == PLAYER_CONNECTED) {
        send_error(player, ERR_NOT_LOGGED_IN);
        return false;
    }
    return true;
}
 
/* Rimette un giocatore in lobby, azzerando la sua flotta */
static void reset_player_to_lobby(Player *player) {
    pthread_mutex_lock(&player->lock);
    player->state = PLAYER_LOBBY;
    player->current_game_id = -1;
    player->wants_rematch = false;
    init_board(&player->board);
    pthread_mutex_unlock(&player->lock);
}
 
/*
 *  una sola funzione che smonta la partita corrente.
 * La usiamo per LEAVE_GAME, per il rifiuto della rivincita e per la
 * disconnessione improvvisa (read() == 0).
 *
 * opponent_notice = messaggio da mandare all'avversario prima di
 * rimandarlo in lobby (OPPONENT_DISCONNECTED oppure REMATCH_REJECTED).
 * notify_self = se true manda BACK_TO_LOBBY anche a chi esce (falso quando
 * il socket e' gia' caduto).
 */
static void leave_current_game(GameManager *gm, Player *player,
                               ResponseType opponent_notice, bool notify_self) {
    int game_id = get_player_game_id(player);
 
    if (game_id > 0) {
        Game *game = gm_get_game(gm, game_id);
        int opponent_id = -1;
 
        if (game) {
            pthread_mutex_lock(&game->lock);
            if (game->creator_id == player->id) {
                opponent_id = game->opponent_id;
            } else {
                opponent_id = game->creator_id;
            }
            game->state = GAME_FINISHED;
            pthread_mutex_unlock(&game->lock);
        }
 
        if (opponent_id > 0) {
            Player *opponent = gm_get_player(gm, opponent_id);
            if (opponent) {
                /* controlliamo che sia davvero il compagno di QUESTA partita */
                pthread_mutex_lock(&opponent->lock);
                bool same_game = (opponent->current_game_id == game_id);
                pthread_mutex_unlock(&opponent->lock);
 
                if (same_game) {
                    send_simple(opponent, opponent_notice);
                    reset_player_to_lobby(opponent);
                    send_simple(opponent, RSP_BACK_TO_LOBBY);
                }
            }
        }
 
        if (game) {
            gm_remove_game(gm, game_id);
        }
    }
 
    reset_player_to_lobby(player);
    if (notify_self) {
        send_simple(player, RSP_BACK_TO_LOBBY);
    }
}
 
/*
 * Disconnessione (socket chiuso o comando QUIT).
 * current_game_id viene letto dentro il lock del player.
 */
static void handle_disconnect(GameManager *gm, Player *player) {
    if (!player) return;
 
    char name[MAX_USERNAME];
    gm_copy_username(player, name, sizeof(name));
    printf("[HANDLER] Disconnessione rilevata per player '%s' (ID %d)\n", name, player->id);
 
    leave_current_game(gm, player, RSP_OPPONENT_DISCONNECTED, false);
    gm_remove_player(gm, player->id);
}
 
/* ------------------------------------------------------------------ */
/* Comandi                                                            */
/* ------------------------------------------------------------------ */
 
static void handle_login(GameManager *gm, Player *player, const Message *msg) {
    char resp[MAX_MESSAGE_LEN];
 
    /*  niente doppio login */
    if (get_player_state(player) != PLAYER_CONNECTED) {
        send_error(player, ERR_WRONG_STATE);
        return;
    }
//controllo parametri
    if (msg->param_count < 2) {
        send_error(player, ERR_INVALID_PARAMS);
        return;
    }
//controllo che è stato passato username come parametro
    const char *desired_username = msg->params[1];
    if (strlen(desired_username) == 0 || strlen(desired_username) >= MAX_USERNAME) {
        build_response(RSP_ERROR, resp, sizeof(resp), 2, "101", "Lunghezza username non valida");
        gm_notify_player(player, resp);
        return;
    }
//controllo che lo username non sia gia occupato
    if (!gm_set_username(gm, player->id, desired_username)) {
        send_error(player, ERR_USERNAME_TAKEN);
        return;
    }
//se è tutto ok, imposta lo stato player_lobby
    pthread_mutex_lock(&player->lock);
    player->state = PLAYER_LOBBY;
    pthread_mutex_unlock(&player->lock);
//invia welcome
    char id_str[16];
    snprintf(id_str, sizeof(id_str), "%d", player->id);
    build_response(RSP_WELCOME, resp, sizeof(resp), 1, id_str);
    gm_notify_player(player, resp);
    printf("[HANDLER] Utente '%s' ha effettuato il login (ID %d)\n", desired_username, player->id);
}
 
static void handle_create_game(GameManager *gm, Player *player) {
    char resp[MAX_MESSAGE_LEN];
 
    if (get_player_state(player) != PLAYER_LOBBY) {
        send_error(player, ERR_WRONG_STATE);
        return;
    }
 
    Game *g = gm_create_game(gm, player->id);
    if (!g) {
        build_response(RSP_ERROR, resp, sizeof(resp), 2, "500", "Limite massimo di 32 partite raggiunto");
        gm_notify_player(player, resp);
        return;
    }
 
    char code[GAME_CODE_LEN];
    pthread_mutex_lock(&g->lock);
    strncpy(code, g->game_code, sizeof(code) - 1);
    code[sizeof(code) - 1] = '\0';
    int game_id = g->id;
    pthread_mutex_unlock(&g->lock);
 
    pthread_mutex_lock(&player->lock);
    player->current_game_id = game_id;
    player->state = PLAYER_WAITING_OPPONENT;
    pthread_mutex_unlock(&player->lock);
 
    build_response(RSP_GAME_CREATED, resp, sizeof(resp), 1, code);
    gm_notify_player(player, resp);
 
    char name[MAX_USERNAME];
    gm_copy_username(player, name, sizeof(name));
    printf("[HANDLER] Nuova partita creata con codice '%s' da '%s'\n", code, name);
}
 
static void handle_list_games(GameManager *gm, Player *player) {
    char list_buffer[MAX_MESSAGE_LEN];
    list_buffer[0] = '\0';
 
    /*  GameInfo al posto della copia dell'intera struct Game */
    GameInfo available_games[MAX_GAMES];
    int count = gm_get_available_games(gm, available_games, MAX_GAMES);
 
    size_t offset = 0;
    for (int i = 0; i < count && offset < sizeof(list_buffer) - 1; i++) {
        Player *creator = gm_get_player(gm, available_games[i].creator_id);
        char cname[MAX_USERNAME];
        gm_copy_username(creator, cname, sizeof(cname));
        if (cname[0] == '\0') {
            strncpy(cname, "Sconosciuto", sizeof(cname) - 1);
            cname[sizeof(cname) - 1] = '\0';
        }
 
        int written = snprintf(list_buffer + offset, sizeof(list_buffer) - offset,
                               "%s%s:%s", (i > 0) ? "," : "",
                               available_games[i].game_code, cname);
        if (written > 0) offset += (size_t)written;
    }
 
    char resp[MAX_MESSAGE_LEN];
    build_response(RSP_GAME_LIST, resp, sizeof(resp), 1, list_buffer);
    gm_notify_player(player, resp);
}
 
static void handle_join_game(GameManager *gm, Player *player, const Message *msg) {
    char resp[MAX_MESSAGE_LEN];
 
    /* si puo' chiedere di entrare solo se si e' liberi in lobby */
    if (get_player_state(player) != PLAYER_LOBBY) {
        send_error(player, ERR_WRONG_STATE);
        return;
    }
 
    if (msg->param_count < 2) {
        send_error(player, ERR_INVALID_PARAMS);
        return;
    }
//estraggo codice partita come parametro e cerco la struttura Game corrispondente
    const char *code = msg->params[1];
    Game *g = gm_get_game_by_code(gm, code);
    if (!g) {
        send_error(player, ERR_GAME_NOT_FOUND);
        return;
    }
 
    pthread_mutex_lock(&g->lock);
 
    /*  non ci si puo' unire alla propria partita. */
    if (g->creator_id == player->id) {
        pthread_mutex_unlock(&g->lock);
        send_error(player, ERR_CANNOT_JOIN_OWN_GAME);
        return;
    }
//la partita deve essere in attesa di giocatori e non deve avere già
//un avversario assegnato (opponent_id=-1)
    if (g->state != GAME_WAITING_PLAYERS || g->opponent_id != -1) {
        pthread_mutex_unlock(&g->lock);
        send_error(player, ERR_GAME_FULL);
        return;
    }
//se è tutto apposto, il campo pending_invite_from viene settato con l'id
//del giocatore richiedente
    g->pending_invite_from = player->id;
    int creator_id = g->creator_id;
    pthread_mutex_unlock(&g->lock);
 
    /* il richiedente resta "in attesa di risposta" */
    pthread_mutex_lock(&player->lock);
    player->state = PLAYER_INVITED;
    pthread_mutex_unlock(&player->lock);
//cerco creatore e mando messaggio di richiesta unione
    Player *creator = gm_get_player(gm, creator_id);
    if (creator) {
        char req[MAX_MESSAGE_LEN];
        char p_id_str[16];
        char name[MAX_USERNAME];
        gm_copy_username(player, name, sizeof(name));
        snprintf(p_id_str, sizeof(p_id_str), "%d", player->id);
        build_response(RSP_JOIN_REQUEST, req, sizeof(req), 2, name, p_id_str);
        gm_notify_player(creator, req);
    }
//mando messaggio esito positivo al giocatore che ha fatto richiesta
    build_response(RSP_OK, resp, sizeof(resp), 1, "Richiesta inviata al creatore");
    gm_notify_player(player, resp);
}
 
/* accetta il giocatore indicato dal creatore: accetta sia l'id che lo username */
static Player* resolve_target_player(GameManager *gm, const char *param) {
    Player *target = NULL;
    int id = atoi(param);
    if (id > 0) {
        target = gm_get_player(gm, id);
    }
    if (!target) {
        target = gm_get_player_by_username(gm, param);
    }
    return target;
}
 
static void handle_accept_invite(GameManager *gm, Player *player, const Message *msg) {
    char resp[MAX_MESSAGE_LEN];
 
    if (msg->param_count < 2) {
        send_error(player, ERR_INVALID_PARAMS);
        return;
    }
 
    if (get_player_state(player) != PLAYER_WAITING_OPPONENT) {
        send_error(player, ERR_WRONG_STATE);
        return;
    }
 
    Player *requester = resolve_target_player(gm, msg->params[1]);
    if (!requester) {
        build_response(RSP_ERROR, resp, sizeof(resp), 2, "300", "Giocatore non piu' disponibile");
        gm_notify_player(player, resp);
        return;
    }
 
    Game *g = gm_get_game(gm, get_player_game_id(player));
    if (!g) {
        send_error(player, ERR_GAME_NOT_FOUND);
        return;
    }
 
    pthread_mutex_lock(&g->lock);
 
    /*
     * Ora accettiamo SOLO chi ha realmente inviato la richiesta.
     */
    if (g->state != GAME_WAITING_PLAYERS ||
        g->creator_id != player->id ||
        g->pending_invite_from != requester->id) {
        pthread_mutex_unlock(&g->lock);
        send_error(player, ERR_NO_PENDING_INVITE);
        return;
    }
 
    g->opponent_id = requester->id;
    g->pending_invite_from = -1;
    g->state = GAME_PLACING_SHIPS;
    int game_id = g->id;
    char code[GAME_CODE_LEN];
    strncpy(code, g->game_code, sizeof(code) - 1);
    code[sizeof(code) - 1] = '\0';
    pthread_mutex_unlock(&g->lock);
 
    pthread_mutex_lock(&requester->lock);
    requester->current_game_id = game_id;
    requester->state = PLAYER_PLACING_SHIPS;
    init_board(&requester->board);
    pthread_mutex_unlock(&requester->lock);
 
    pthread_mutex_lock(&player->lock);
    player->state = PLAYER_PLACING_SHIPS;
    init_board(&player->board);
    pthread_mutex_unlock(&player->lock);
 
    build_response(RSP_JOIN_ACCEPTED, resp, sizeof(resp), 1, code);
    gm_notify_player(requester, resp);
 
    send_simple(player, RSP_GAME_START);
    send_simple(requester, RSP_GAME_START);
 
    printf("[HANDLER] Partita '%s' iniziata: posizionamento navi\n", code);
}
 
static void handle_reject_invite(GameManager *gm, Player *player, const Message *msg) {
    if (msg->param_count < 2) {
        send_error(player, ERR_INVALID_PARAMS);
        return;
    }
 
    Player *requester = resolve_target_player(gm, msg->params[1]);
    Game *g = gm_get_game(gm, get_player_game_id(player));
    if (!g || !requester) {
        send_error(player, ERR_GAME_NOT_FOUND);
        return;
    }
 
    pthread_mutex_lock(&g->lock);
    /* stesso controllo dell'accept */
    if (g->creator_id != player->id || g->pending_invite_from != requester->id) {
        pthread_mutex_unlock(&g->lock);
        send_error(player, ERR_NO_PENDING_INVITE);
        return;
    }
    g->pending_invite_from = -1;   
    pthread_mutex_unlock(&g->lock);
 
    /* il richiedente torna libero in lobby */
    pthread_mutex_lock(&requester->lock);
    if (requester->state == PLAYER_INVITED) {
        requester->state = PLAYER_LOBBY;
    }
    pthread_mutex_unlock(&requester->lock);
 
    send_simple(requester, RSP_JOIN_REJECTED);
    send_ok(player, "Richiesta rifiutata.");
}
 
static void handle_place_ship(GameManager *gm, Player *player, const Message *msg) {
    (void)gm;
    char resp[MAX_MESSAGE_LEN];
 
    if (get_player_state(player) != PLAYER_PLACING_SHIPS) {
        send_error(player, ERR_WRONG_STATE);
        return;
    }
 
    if (msg->param_count < 5) {
        build_response(RSP_INVALID_PLACEMENT, resp, sizeof(resp), 1, "Parametri insufficienti");
        gm_notify_player(player, resp);
        return;
    }
 
    int r1 = atoi(msg->params[1]);
    int c1 = atoi(msg->params[2]);
    int r2 = atoi(msg->params[3]);
    int c2 = atoi(msg->params[4]);
 
    pthread_mutex_lock(&player->lock);
    PlacementResult res = place_ship(&player->board, r1, c1, r2, c2);
    int placed = player->board.ships_placed;
    pthread_mutex_unlock(&player->lock);
 
    if (res == PLACEMENT_OK) {
        char placed_str[16], r1s[8], c1s[8], r2s[8], c2s[8];
        snprintf(placed_str, sizeof(placed_str), "%d", placed);
        snprintf(r1s, sizeof(r1s), "%d", r1);
        snprintf(c1s, sizeof(c1s), "%d", c1);
        snprintf(r2s, sizeof(r2s), "%d", r2);
        snprintf(c2s, sizeof(c2s), "%d", c2);
        /* SHIP_PLACED|n|r1|c1|r2|c2 */
        build_response(RSP_SHIP_PLACED, resp, sizeof(resp), 5,
                       placed_str, r1s, c1s, r2s, c2s);
    } else {
        const char *err_msg = "Posizionamento non valido";
        if (res == PLACEMENT_OVERLAP)            err_msg = "La nave si sovrappone ad un'altra";
        else if (res == PLACEMENT_OUT_OF_BOUNDS) err_msg = "Nave fuori dai bordi";
        else if (res == PLACEMENT_INVALID_SIZE)  err_msg = "Dimensione nave errata";
        else if (res == PLACEMENT_NOT_ALIGNED)   err_msg = "Nave non allineata";
        else if (res == PLACEMENT_ALL_PLACED)    err_msg = "Hai gia' posizionato tutte le navi";
        build_response(RSP_INVALID_PLACEMENT, resp, sizeof(resp), 1, err_msg);
    }
    gm_notify_player(player, resp);
}
 
static void handle_ready(GameManager *gm, Player *player) {
    char resp[MAX_MESSAGE_LEN];
 
    if (get_player_state(player) != PLAYER_PLACING_SHIPS) {
        send_error(player, ERR_WRONG_STATE);
        return;
    }
 
    pthread_mutex_lock(&player->lock);
    if (!all_ships_placed(&player->board)) {
        pthread_mutex_unlock(&player->lock);
        build_response(RSP_ERROR, resp, sizeof(resp), 2, "101",
                       "Devi prima posizionare tutte e 5 le navi");
        gm_notify_player(player, resp);
        return;
    }
    player->state = PLAYER_READY;
    int current_game_id = player->current_game_id;
    pthread_mutex_unlock(&player->lock);
 
    Game *g = gm_get_game(gm, current_game_id);
    if (!g) {
        send_error(player, ERR_GAME_NOT_FOUND);
        return;
    }
 
    pthread_mutex_lock(&g->lock);
    if (g->creator_id == player->id) {
        g->creator_ready = true;
    } else {
        g->opponent_ready = true;
    }
 
    bool both_ready = g->creator_ready && g->opponent_ready;
    if (both_ready) {
        g->state = GAME_IN_PROGRESS;
        g->current_turn = g->creator_id;
    }
    int creator_id  = g->creator_id;
    int opponent_id = g->opponent_id;
    pthread_mutex_unlock(&g->lock);
 
    if (both_ready) {
        Player *c = gm_get_player(gm, creator_id);
        Player *o = gm_get_player(gm, opponent_id);
 
        if (c && o) {
            pthread_mutex_lock(&c->lock);
            c->state = PLAYER_IN_GAME;
            pthread_mutex_unlock(&c->lock);
 
            pthread_mutex_lock(&o->lock);
            o->state = PLAYER_IN_GAME;
            pthread_mutex_unlock(&o->lock);
 
            send_simple(c, RSP_YOUR_TURN);
            send_simple(o, RSP_WAIT_TURN);
        }
    } else {
        send_ok(player, "In attesa dell'avversario...");
    }
}
 
/*
 * Doppio lock con ordine canonico per evitare il deadlock:
 * si prende sempre prima il lock del player con id minore.
 */
static void handle_fire(GameManager *gm, Player *player, const Message *msg) {
    char resp[MAX_MESSAGE_LEN];
 
    if (get_player_state(player) != PLAYER_IN_GAME) {
        send_error(player, ERR_WRONG_STATE);
        return;
    }
 
    if (msg->param_count < 3) {
        send_error(player, ERR_INVALID_PARAMS);
        return;
    }
 
    Game *g = gm_get_game(gm, get_player_game_id(player));
    if (!g) {
        send_error(player, ERR_GAME_NOT_FOUND);
        return;
    }

 // Se il client invia FIRE quando non è il suo turno, 
 // il server risponde con ERR_NOT_YOUR_TURN.
    pthread_mutex_lock(&g->lock);
    if (g->state != GAME_IN_PROGRESS || g->current_turn != player->id) {
        pthread_mutex_unlock(&g->lock);
        send_error(player, ERR_NOT_YOUR_TURN);
        return;
    }
    //Se chi spara è il creatore, l'avversario è l'opponent_id, altrimenti è il creator_id
    int opponent_id = (g->creator_id == player->id) ? g->opponent_id : g->creator_id;
    pthread_mutex_unlock(&g->lock);
 
    Player *opponent = gm_get_player(gm, opponent_id);
    if (!opponent) {
        send_error(player, ERR_GAME_NOT_FOUND);
        return;
    }
 
    /*
     * protezione contro il caso opponent == player.
     * Senza questo controllo faremmo due pthread_mutex_lock sullo STESSO
     * mutex non ricorsivo e il thread si bloccherebbe per sempre.
     */
    if (opponent->id == player->id) {
        send_error(player, ERR_INTERNAL);
        return;
    }
 
    int row = atoi(msg->params[1]);
    int col = atoi(msg->params[2]);
 //  lock in ordine canonico per evitare deadlock: prima il player con id minore
//Adesso calcolo l'id del player con id minore e quello con id maggiore, e li 
// assegno a first e second. Poi faccio il lock di first e poi di second.
    Player *first  = (player->id < opponent->id) ? player   : opponent;
    Player *second = (player->id < opponent->id) ? opponent : player;
 //Uso due mutex per proteggere l'accesso alle board dei due giocatori. 
 // Con l'ordine canonico entrambi fanno prima il lock del giocatore con id minore 
 // e poi quello con id maggiore,
    pthread_mutex_lock(&first->lock);
    pthread_mutex_lock(&second->lock);
 
    int sunk_size = 0;
    FireResult fres = process_fire(&opponent->board, &player->board, row, col, &sunk_size);
    bool victory = check_victory(&opponent->board); //Controlla se l'avversario ha perso tutte le navi e quindi il giocatore ha vinto.
 
    pthread_mutex_unlock(&second->lock);
    pthread_mutex_unlock(&first->lock);
 //fres può essere FIRE_MISS, FIRE_HIT, FIRE_SUNK o FIRE_INVALID.
    if (fres == FIRE_INVALID) {
        build_response(RSP_ERROR, resp, sizeof(resp), 2, "304",
                       "Coordinata non valida o gia' colpita");
        gm_notify_player(player, resp);
        return;
    }
 
    char r_str[8], c_str[8];
    snprintf(r_str, sizeof(r_str), "%d", row);
    snprintf(c_str, sizeof(c_str), "%d", col);
 
    char enemy_notify[MAX_MESSAGE_LEN];
    const char *outcome_str = (fres == FIRE_MISS) ? "MISS" : ((fres == FIRE_HIT) ? "HIT" : "SUNK");
    build_response(RSP_ENEMY_FIRE, enemy_notify, sizeof(enemy_notify), 3, r_str, c_str, outcome_str);
    gm_notify_player(opponent, enemy_notify);
 
    if (fres == FIRE_MISS) {
        build_response(RSP_MISS, resp, sizeof(resp), 2, r_str, c_str);
    } else if (fres == FIRE_HIT) {
        build_response(RSP_HIT, resp, sizeof(resp), 2, r_str, c_str);
    } else {
        char size_str[8];
        snprintf(size_str, sizeof(size_str), "%d", sunk_size);
        build_response(RSP_SUNK, resp, sizeof(resp), 3, r_str, c_str, size_str);
    }
    gm_notify_player(player, resp);
 
    if (victory) {
        pthread_mutex_lock(&g->lock);
        g->state = GAME_FINISHED;
        g->winner_id = player->id;
        g->creator_wants_rematch = false;
        g->opponent_wants_rematch = false;
        pthread_mutex_unlock(&g->lock);
 
        /*
         * a fine partita i due giocatori passano in
         * PLAYER_FINISHED.Da qui possono chiedere la rivincita
         * oppure uscire.
         */
        pthread_mutex_lock(&player->lock);
        player->state = PLAYER_FINISHED;
        pthread_mutex_unlock(&player->lock);
 
        pthread_mutex_lock(&opponent->lock);
        opponent->state = PLAYER_FINISHED;
        pthread_mutex_unlock(&opponent->lock);
 
        send_simple(player, RSP_YOU_WIN);
        send_simple(opponent, RSP_YOU_LOSE);
 
        /* prompt "vuoi rigiocare?" a entrambi */
        send_simple(player, RSP_PLAY_AGAIN_PROMPT);
        send_simple(opponent, RSP_PLAY_AGAIN_PROMPT);
 
        printf("[HANDLER] Partita %d terminata, vincitore ID %d\n", g->id, player->id);
        return;
    }
 
    pthread_mutex_lock(&g->lock);
    g->current_turn = opponent_id;
    pthread_mutex_unlock(&g->lock);
 
    send_simple(player, RSP_WAIT_TURN);
    send_simple(opponent, RSP_YOUR_TURN);
}
 
/*
 * LEAVE_GAME. Esce dalla partita ma NON chiude la connessione,
 * cosi' il giocatore torna nella lobby
 */
static void handle_leave_game(GameManager *gm, Player *player) {
    if (get_player_game_id(player) <= 0) {
        reset_player_to_lobby(player);
        send_simple(player, RSP_BACK_TO_LOBBY);
        return;
    }
    leave_current_game(gm, player, RSP_OPPONENT_DISCONNECTED, true);
}
 
/*
 * la rivincita.
 * Entrambi devono dire di si': solo allora la stessa partita riparte
 * dalla fase di posizionamento con la stessa coppia di giocatori.
 */
static void handle_rematch(GameManager *gm, Player *player) {
    if (get_player_state(player) != PLAYER_FINISHED) {
        send_error(player, ERR_REMATCH_NOT_AVAILABLE);
        return;
    }
 
    Game *g = gm_get_game(gm, get_player_game_id(player));
    if (!g) {
        send_error(player, ERR_REMATCH_NOT_AVAILABLE);
        reset_player_to_lobby(player);
        send_simple(player, RSP_BACK_TO_LOBBY);
        return;
    }
 
    pthread_mutex_lock(&g->lock);
    if (g->state != GAME_FINISHED) {
        pthread_mutex_unlock(&g->lock);
        send_error(player, ERR_REMATCH_NOT_AVAILABLE);
        return;
    }
 
    int opponent_id;
    if (g->creator_id == player->id) {
        g->creator_wants_rematch = true;
        opponent_id = g->opponent_id;
    } else {
        g->opponent_wants_rematch = true;
        opponent_id = g->creator_id;
    }
 
    bool both = g->creator_wants_rematch && g->opponent_wants_rematch;
    if (both) {
        reset_game_for_rematch(g);   /* torna a GAME_PLACING_SHIPS */
    }
    pthread_mutex_unlock(&g->lock);
 
    Player *opponent = gm_get_player(gm, opponent_id);
    if (!opponent) {
        /* l'avversario e' sparito nel frattempo */
        send_error(player, ERR_REMATCH_NOT_AVAILABLE);
        leave_current_game(gm, player, RSP_OPPONENT_DISCONNECTED, true);
        return;
    }
 
    if (both) {
        pthread_mutex_lock(&player->lock);
        player->state = PLAYER_PLACING_SHIPS;
        player->wants_rematch = false;
        init_board(&player->board);
        pthread_mutex_unlock(&player->lock);
 
        pthread_mutex_lock(&opponent->lock);
        opponent->state = PLAYER_PLACING_SHIPS;
        opponent->wants_rematch = false;
        init_board(&opponent->board);
        pthread_mutex_unlock(&opponent->lock);
 
        /* GAME_START azzera le griglie anche lato client */
        send_simple(player, RSP_GAME_START);
        send_simple(opponent, RSP_GAME_START);
        printf("[HANDLER] Rivincita accettata: la partita %d riparte\n", g->id);
    } else {
        pthread_mutex_lock(&player->lock);
        player->wants_rematch = true;
        pthread_mutex_unlock(&player->lock);
 
        send_ok(player, "Richiesta di rivincita inviata, aspetto l'avversario...");
        send_simple(opponent, RSP_REMATCH_REQUEST);
    }
}
 
/*
 * rifiuto della rivincita = "uscire definitivamente dalla
 * sessione di gioco". La partita viene chiusa, lo slot liberato ed
 * entrambi tornano in lobby, pronti a creare NUOVE partite.
 */
static void handle_rematch_decline(GameManager *gm, Player *player) {
    if (get_player_state(player) != PLAYER_FINISHED) {
        send_error(player, ERR_REMATCH_NOT_AVAILABLE);
        return;
    }
    leave_current_game(gm, player, RSP_REMATCH_REJECTED, true);
}
 
/* ------------------------------------------------------------------ */
/* Thread worker                                                      */
/* ------------------------------------------------------------------ */
 
void* client_handler_thread(void *arg) {
    //Recupero gli argomenti passati al thread e 
    // li libero subito dopo averli copiati in variabili locali.
    ThreadArgs *args = (ThreadArgs*)arg;
    int socket_fd = args->socket_fd;
    GameManager *gm = args->gm;
    free(args);
 //Registro il client appena connesso nel GameManager. 
 
    Player *player = gm_add_player(gm, socket_fd);
    if (!player) {
        /* server pieno: rispondiamo prima di chiudere */
        char resp[MAX_MESSAGE_LEN];
        build_response(RSP_ERROR, resp, sizeof(resp), 2, "500", error_to_string(ERR_SERVER_FULL));
        send_message(socket_fd, resp);
        close(socket_fd);
        pthread_exit(NULL);
    }
 
    char buffer[MAX_MESSAGE_LEN];
    Message msg;
    bool running = true;
 
    while (running) { // ciclo principale di lettura continuo dei messaggi dal client 
        int bytes = receive_message(socket_fd, buffer, sizeof(buffer));
        if (bytes <= 0) break;   // Se il client si disconnette o c'è un errore di lettura, esci dal ciclo
        
        //Il messaggio testuale ricevuto viene trasformato nella struttura msg
        if (!parse_message(buffer, &msg)) {
            //Se il parsing fallisce manda errore al client
            send_error(player, ERR_INVALID_COMMAND);
            continue;
        }
 
        /* tutto tranne LOGIN e QUIT richiede l'autenticazione */
        if (msg.type != CMD_LOGIN && msg.type != CMD_QUIT && !require_login(player)) {
            continue;
        }
        //In base al tipo di comando ricevuto, viene chiamata 
        // la funzione corrispondente per gestire il comando.
        switch (msg.type) {
            case CMD_LOGIN:
                handle_login(gm, player, &msg);
                break;
            case CMD_CREATE_GAME:
                handle_create_game(gm, player);
                break;
            case CMD_LIST_GAMES:
                handle_list_games(gm, player);
                break;
            case CMD_JOIN_GAME:
                handle_join_game(gm, player, &msg);
                break;
            case CMD_ACCEPT_INVITE:
                handle_accept_invite(gm, player, &msg);
                break;
            case CMD_REJECT_INVITE:
                handle_reject_invite(gm, player, &msg);
                break;
            case CMD_PLACE_SHIP:
                handle_place_ship(gm, player, &msg);
                break;
            case CMD_READY:
                handle_ready(gm, player);
                break;
            case CMD_FIRE:
                handle_fire(gm, player, &msg);
                break;
            case CMD_LEAVE_GAME:
                handle_leave_game(gm, player);
                break;
            case CMD_REMATCH:
                handle_rematch(gm, player);
                break;
            case CMD_REMATCH_DECLINE:
                handle_rematch_decline(gm, player);
                break;
            case CMD_QUIT:
                running = false;
                break;
            default:
                send_error(player, ERR_INVALID_COMMAND);
                break;
        }
    }
 //Quando il thread termina:
 // viene gestita la disconnessione del giocatore, 
 // chiuso il socket e terminato il thread.
    handle_disconnect(gm, player);
    close(socket_fd);
    pthread_exit(NULL);
}
