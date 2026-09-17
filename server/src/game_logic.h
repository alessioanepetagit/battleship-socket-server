#ifndef GAME_LOGIC_H
#define GAME_LOGIC_H

#include <stdbool.h>
#include <pthread.h>
#include <time.h>

#define GRID_SIZE       10
#define MAX_SHIPS       5
#define MAX_USERNAME    32
#define GAME_CODE_LEN   7

static const int SHIP_SIZES[MAX_SHIPS] = {5, 4, 3, 3, 2};

typedef enum {
    CELL_EMPTY = 0,
    CELL_SHIP,
    CELL_HIT,
    CELL_MISS
} CellState;

typedef enum {
    PLAYER_DISCONNECTED = 0,
    PLAYER_CONNECTED,          /* MODIFICA: connesso ma NON ancora loggato */
    PLAYER_LOBBY,
    PLAYER_WAITING_OPPONENT,
    PLAYER_INVITED,
    PLAYER_PLACING_SHIPS,
    PLAYER_READY,
    PLAYER_IN_GAME,
    PLAYER_FINISHED            /* partita finita: puo' chiedere la rivincita */
} PlayerState;

typedef enum {
    GAME_WAITING_PLAYERS = 0,
    GAME_PLACING_SHIPS,
    GAME_IN_PROGRESS,
    GAME_FINISHED
} GameState;

typedef enum {
    FIRE_MISS = 0,
    FIRE_HIT,
    FIRE_SUNK,
    FIRE_INVALID
} FireResult;

typedef struct {
    int start_row, start_col;
    int end_row, end_col;
    int size;
    int hits;
    bool is_sunk;
} Ship;

typedef struct {
    CellState cells[GRID_SIZE][GRID_SIZE];
    CellState enemy_view[GRID_SIZE][GRID_SIZE];
    Ship ships[MAX_SHIPS];
    int ships_placed;
    int ships_remaining;
} Board;

typedef struct {
    int id;
    int socket_fd;
    char username[MAX_USERNAME];
    PlayerState state;
    int current_game_id;
    Board board;
    bool wants_rematch;
    pthread_mutex_t lock;
} Player;

typedef struct {
    int id;
    char game_code[GAME_CODE_LEN];
    GameState state;
    int creator_id;
    int opponent_id;
    int pending_invite_from;
    int current_turn;
    bool creator_ready;
    bool opponent_ready;
    int winner_id;
    time_t created_at;
    time_t last_activity;
    bool creator_wants_rematch;
    bool opponent_wants_rematch;
    pthread_mutex_t lock;
} Game;

typedef enum {
    PLACEMENT_OK = 0,
    PLACEMENT_OUT_OF_BOUNDS,
    PLACEMENT_OVERLAP,
    PLACEMENT_INVALID_SIZE,
    PLACEMENT_NOT_ALIGNED,
    PLACEMENT_ALL_PLACED
} PlacementResult;

void init_board(Board *board);

/*
 * MODIFICA IMPORTANTE:
 * init_player() e init_game() NON inizializzano piu' il mutex della struttura.
 * I mutex degli slot vengono creati una volta sola in gm_init() e distrutti
 * una volta sola in gm_destroy(). Prima venivano distrutti alla disconnessione
 * mentre un altro thread poteva ancora averne il puntatore -> undefined
 * behavior. Ora lo slot viene solo "svuotato" e riusato, ma il suo mutex resta
 * sempre valido per tutta la vita del server.
 */
void init_player(Player *player, int id, int socket_fd);
void init_game(Game *game, int id, int creator_id);
void generate_game_code(char *code, size_t len);

/* MODIFICA: azzera i flag della partita per far ripartire una nuova sfida
   fra gli stessi due giocatori. Il chiamante deve tenere game->lock. */
void reset_game_for_rematch(Game *game);

PlacementResult place_ship(Board *board, int start_row, int start_col, int end_row, int end_col);
bool all_ships_placed(const Board *board);
int get_next_ship_size(const Board *board);

FireResult process_fire(Board *target_board, Board *attacker_board, int row, int col, int *sunk_ship_size);
bool check_victory(const Board *target_board);
bool is_valid_coords(int row, int col);
bool is_already_fired(const Board *attacker_board, int row, int col);

bool parse_coords(const char *coords_str, int *row, int *col);
void print_board(const Board *board, bool show_ships);
const char* game_state_to_string(GameState state);
const char* player_state_to_string(PlayerState state);

#endif
