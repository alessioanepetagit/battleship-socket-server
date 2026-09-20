#include "game_logic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

void init_board(Board *board) {
    if (!board) return;

    for (int i = 0; i < GRID_SIZE; i++) {
        for (int j = 0; j < GRID_SIZE; j++) {
            board->cells[i][j] = CELL_EMPTY;
            board->enemy_view[i][j] = CELL_EMPTY;
        }
    }

    for (int i = 0; i < MAX_SHIPS; i++) {
        board->ships[i].start_row = -1;
        board->ships[i].start_col = -1;
        board->ships[i].end_row = -1;
        board->ships[i].end_col = -1;
        board->ships[i].size = SHIP_SIZES[i];
        board->ships[i].hits = 0;
        board->ships[i].is_sunk = false;
    }

    board->ships_placed = 0;
    board->ships_remaining = MAX_SHIPS;
}

void init_player(Player *player, int id, int socket_fd) {
    if (!player) return;

    player->id = id;
    player->socket_fd = socket_fd;
    memset(player->username, 0, MAX_USERNAME);
    /* MODIFICA: si parte da PLAYER_CONNECTED, non da PLAYER_LOBBY.
       Cosi' nessun comando di gioco passa prima della LOGIN. */
    player->state = PLAYER_CONNECTED;
    player->current_game_id = -1;
    player->wants_rematch = false;

    init_board(&player->board);
}

void init_game(Game *game, int id, int creator_id) {
    if (!game) return;

    game->id = id;
    generate_game_code(game->game_code, GAME_CODE_LEN);
    game->state = GAME_WAITING_PLAYERS;
    game->creator_id = creator_id;
    game->opponent_id = -1;
    game->pending_invite_from = -1;
    game->current_turn = -1;
    game->creator_ready = false;
    game->opponent_ready = false;
    game->creator_wants_rematch = false;
    game->opponent_wants_rematch = false;
    game->winner_id = -1;
    game->created_at = time(NULL);
    game->last_activity = time(NULL);

}


void reset_game_for_rematch(Game *game) {
    if (!game) return;

    game->state = GAME_PLACING_SHIPS;
    game->current_turn = -1;
    game->creator_ready = false;
    game->opponent_ready = false;
    game->creator_wants_rematch = false;
    game->opponent_wants_rematch = false;
    game->winner_id = -1;
    game->pending_invite_from = -1;
    game->last_activity = time(NULL);
}

void generate_game_code(char *code, size_t len) {
    if (!code || len < 7) return;

    static const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    static int seeded = 0;

    if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = 1;
    }

    for (size_t i = 0; i < 6; i++) {
        code[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    code[6] = '\0';
}

static bool ships_overlap(const Ship *ship1, int r1, int c1, int r2, int c2) {
    int min_r = (r1 < r2) ? r1 : r2;
    int max_r = (r1 > r2) ? r1 : r2;
    int min_c = (c1 < c2) ? c1 : c2;
    int max_c = (c1 > c2) ? c1 : c2;

    int ship_min_r = (ship1->start_row < ship1->end_row) ? ship1->start_row : ship1->end_row;
    int ship_max_r = (ship1->start_row > ship1->end_row) ? ship1->start_row : ship1->end_row;
    int ship_min_c = (ship1->start_col < ship1->end_col) ? ship1->start_col : ship1->end_col;
    int ship_max_c = (ship1->start_col > ship1->end_col) ? ship1->start_col : ship1->end_col;

    bool overlap_r = (min_r <= ship_max_r) && (max_r >= ship_min_r);
    bool overlap_c = (min_c <= ship_max_c) && (max_c >= ship_min_c);

    return overlap_r && overlap_c;
}

PlacementResult place_ship(Board *board, int start_row, int start_col, int end_row, int end_col) {
    if (!board) return PLACEMENT_INVALID_SIZE;

    if (board->ships_placed >= MAX_SHIPS) {
        return PLACEMENT_ALL_PLACED;
    }

    if (start_row < 0 || start_row >= GRID_SIZE ||
        start_col < 0 || start_col >= GRID_SIZE ||
        end_row < 0 || end_row >= GRID_SIZE ||
        end_col < 0 || end_col >= GRID_SIZE) {
        return PLACEMENT_OUT_OF_BOUNDS;
    }

    bool is_horizontal = (start_row == end_row);
    bool is_vertical = (start_col == end_col);

    if (!is_horizontal && !is_vertical) {
        return PLACEMENT_NOT_ALIGNED;
    }

    int size;
    if (is_horizontal) {
        size = abs(end_col - start_col) + 1;
    } else {
        size = abs(end_row - start_row) + 1;
    }

    int expected_size = SHIP_SIZES[board->ships_placed];
    if (size != expected_size) {
        return PLACEMENT_INVALID_SIZE;
    }

    for (int i = 0; i < board->ships_placed; i++) {
        if (ships_overlap(&board->ships[i], start_row, start_col, end_row, end_col)) {
            return PLACEMENT_OVERLAP;
        }
    }

    Ship *ship = &board->ships[board->ships_placed];
    ship->start_row = start_row;
    ship->start_col = start_col;
    ship->end_row = end_row;
    ship->end_col = end_col;
    ship->size = size;
    ship->hits = 0;
    ship->is_sunk = false;

    int min_r = (start_row < end_row) ? start_row : end_row;
    int max_r = (start_row > end_row) ? start_row : end_row;
    int min_c = (start_col < end_col) ? start_col : end_col;
    int max_c = (start_col > end_col) ? start_col : end_col;

    for (int r = min_r; r <= max_r; r++) {
        for (int c = min_c; c <= max_c; c++) {
            board->cells[r][c] = CELL_SHIP;
        }
    }

    board->ships_placed++;
    return PLACEMENT_OK;
}

bool all_ships_placed(const Board *board) {
    return board && board->ships_placed >= MAX_SHIPS;
}

int get_next_ship_size(const Board *board) {
    if (!board || board->ships_placed >= MAX_SHIPS) return 0;
    return SHIP_SIZES[board->ships_placed];
}

static Ship* find_ship_at(Board *board, int row, int col) {
    for (int i = 0; i < board->ships_placed; i++) {
        Ship *ship = &board->ships[i];
        int min_r = (ship->start_row < ship->end_row) ? ship->start_row : ship->end_row;
        int max_r = (ship->start_row > ship->end_row) ? ship->start_row : ship->end_row;
        int min_c = (ship->start_col < ship->end_col) ? ship->start_col : ship->end_col;
        int max_c = (ship->start_col > ship->end_col) ? ship->start_col : ship->end_col;

        if (row >= min_r && row <= max_r && col >= min_c && col <= max_c) {
            return ship;
        }
    }
    return NULL;
}

FireResult process_fire(Board *target_board, Board *attacker_board, int row, int col, int *sunk_ship_size) {
    if (!target_board || !attacker_board) return FIRE_INVALID;

    if (!is_valid_coords(row, col)) {
        return FIRE_INVALID;
    }

    if (is_already_fired(attacker_board, row, col)) {
        return FIRE_INVALID;
    }

    CellState cell = target_board->cells[row][col];

    if (cell == CELL_SHIP) {
        target_board->cells[row][col] = CELL_HIT;
        attacker_board->enemy_view[row][col] = CELL_HIT;

        Ship *ship = find_ship_at(target_board, row, col);
        if (ship) {
            ship->hits++;
            if (ship->hits >= ship->size) {
                ship->is_sunk = true;
                target_board->ships_remaining--;
                if (sunk_ship_size) {
                    *sunk_ship_size = ship->size;
                }
                return FIRE_SUNK;
            }
        }
        return FIRE_HIT;
    } else if (cell == CELL_EMPTY) {
        target_board->cells[row][col] = CELL_MISS;
        attacker_board->enemy_view[row][col] = CELL_MISS;
        return FIRE_MISS;
    }

    return FIRE_INVALID;
}

bool check_victory(const Board *target_board) {
    return target_board && target_board->ships_remaining <= 0;
}

bool is_valid_coords(int row, int col) {
    return row >= 0 && row < GRID_SIZE && col >= 0 && col < GRID_SIZE;
}

bool is_already_fired(const Board *attacker_board, int row, int col) {
    if (!attacker_board) return true;
    CellState view = attacker_board->enemy_view[row][col];
    return view == CELL_HIT || view == CELL_MISS;
}

bool parse_coords(const char *coords_str, int *row, int *col) {
    if (!coords_str || !row || !col) return false;
    size_t len = strlen(coords_str);
    if (len < 2 || len > 3) return false;

    char col_char = (char)toupper((unsigned char)coords_str[0]);
    if (col_char < 'A' || col_char > 'J') return false;
    *col = col_char - 'A';

    int row_num = atoi(coords_str + 1);
    if (row_num < 1 || row_num > 10) return false;
    *row = row_num - 1;

    return true;
}

void print_board(const Board *board, bool show_ships) {
    if (!board) return;

    printf("   ");
    for (int c = 0; c < GRID_SIZE; c++) {
        printf(" %c ", 'A' + c);
    }
    printf("\n");

    for (int r = 0; r < GRID_SIZE; r++) {
        printf("%2d ", r + 1);
        for (int c = 0; c < GRID_SIZE; c++) {
            CellState cell = show_ships ? board->cells[r][c] : board->enemy_view[r][c];
            char ch;
            switch (cell) {
                case CELL_EMPTY: ch = '.'; break;
                case CELL_SHIP:  ch = show_ships ? '#' : '.'; break;
                case CELL_HIT:   ch = 'X'; break;
                case CELL_MISS:  ch = 'O'; break;
                default:         ch = '?'; break;
            }
            printf(" %c ", ch);
        }
        printf("\n");
    }
}

const char* game_state_to_string(GameState state) {
    switch (state) {
        case GAME_WAITING_PLAYERS: return "In attesa di giocatori";
        case GAME_PLACING_SHIPS:   return "Posizionamento navi";
        case GAME_IN_PROGRESS:     return "In corso";
        case GAME_FINISHED:        return "Terminata";
        default:                   return "Sconosciuto";
    }
}

const char* player_state_to_string(PlayerState state) {
    switch (state) {
        case PLAYER_DISCONNECTED:     return "Disconnesso";
        case PLAYER_CONNECTED:        return "Connesso (non loggato)";
        case PLAYER_LOBBY:            return "In lobby";
        case PLAYER_WAITING_OPPONENT: return "In attesa avversario";
        case PLAYER_INVITED:          return "Invitato";
        case PLAYER_PLACING_SHIPS:    return "Posizionamento navi";
        case PLAYER_READY:            return "Pronto";
        case PLAYER_IN_GAME:          return "In partita";
        case PLAYER_FINISHED:         return "Partita terminata";
        default:                      return "Sconosciuto";
    }
}
