/*
 * ui.c è Implementazione interfaccia utente terminale
 */

#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>

static char cell_to_char(char cell) {
    switch (cell) {
        case 'S': return '#';
        case 'H': return 'X';
        case 'M': return 'O';
        case '.':
        default:  return '~';
    }
}

static const char* cell_to_color(char cell) {
    switch (cell) {
        case 'S': return COLOR_GREEN;
        case 'H': return COLOR_RED;
        case 'M': return COLOR_YELLOW;
        default:  return COLOR_BLUE;
    }
}

void ui_clear_screen(void) {
    printf("\033[H\033[J");
    fflush(stdout);
}

void ui_show_banner(void) {
    printf("\n" COLOR_CYAN COLOR_BOLD);
    printf("╭───────────────────────────────────────────────────────────────────────╮\n");
    printf("│                     ~  S F I D A   N A V A L E  ~                     │\n");
    printf("│                       arena multiplayer online                        │\n");
    printf("╰───────────────────────────────────────────────────────────────────────╯\n");
    printf(COLOR_RESET "\n");
}

void ui_show_lobby_menu(void) {
    printf("\n" COLOR_BOLD "~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~\n" COLOR_RESET);
    printf(COLOR_CYAN "                     LOBBY\n" COLOR_RESET);
    printf(COLOR_BOLD "~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~\n\n" COLOR_RESET);
    printf("  " COLOR_GREEN "1" COLOR_RESET ") Crea una sfida\n");
    printf("  " COLOR_GREEN "2" COLOR_RESET ") Guarda le sfide aperte\n");
    printf("  " COLOR_GREEN "3" COLOR_RESET ") Entra con un codice\n");
    printf("  " COLOR_GREEN "q" COLOR_RESET ") Esci dal gioco\n\n");
    printf(COLOR_BOLD "~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~\n" COLOR_RESET);
    fflush(stdout);
}

static void print_grid(const char board[GRID_SIZE][GRID_SIZE], const char *title, bool show_ships) {
    printf(COLOR_BOLD "\n   %s\n\n" COLOR_RESET, title);
    printf("     A  B  C  D  E  F  G  H  I  J\n");
    printf("   ┌────────────────────────────┐\n");

    for (int r = 0; r < GRID_SIZE; r++) {
        printf("%2d │", r + 1);
        for (int c = 0; c < GRID_SIZE; c++) {
            char cell = board[r][c];
            char display = cell_to_char(cell);
            if (!show_ships && cell == 'S') {
                display = '~';
            }
            printf("%s %c " COLOR_RESET, cell_to_color(cell), display);
        }
        printf("│\n");
    }

    printf("   └────────────────────────────┘\n");
}

void ui_show_boards(const Client *client) {
    ui_clear_screen();
    ui_show_banner();

    printf(COLOR_BOLD "  SFIDA: " COLOR_CYAN "%-8s" COLOR_RESET COLOR_BOLD " | GIOCATORE: " COLOR_GREEN "%s\n\n" COLOR_RESET,
           client->current_game_code[0] ? client->current_game_code : "-", client->username);

    printf(COLOR_BOLD COLOR_YELLOW "            [ ACQUE NEMICHE ]                        [ LA TUA FLOTTA ]\n" COLOR_RESET);
    printf("    A  B  C  D  E  F  G  H  I  J          A  B  C  D  E  F  G  H  I  J\n");
    printf("   ┌────────────────────────────┐        ┌────────────────────────────┐\n");

    for (int r = 0; r < GRID_SIZE; r++) {
        printf("%2d │", r + 1);
        for (int c = 0; c < GRID_SIZE; c++) {
            char cell = client->enemy_board[r][c];
            char display = cell_to_char(cell);
            if (cell == 'S') display = '~';
            printf("%s %c " COLOR_RESET, cell_to_color(cell), display);
        }
        printf("│     ");

        printf("%2d │", r + 1);
        for (int c = 0; c < GRID_SIZE; c++) {
            char cell = client->my_board[r][c];
            char display = cell_to_char(cell);
            printf("%s %c " COLOR_RESET, cell_to_color(cell), display);
        }
        printf("│\n");
    }

    printf("   └────────────────────────────┘        └────────────────────────────┘\n");
    printf(COLOR_GRAY " Legenda: " COLOR_BLUE "~ acqua  " COLOR_GREEN "# nave  " COLOR_RED "X colpito  " COLOR_YELLOW "O mancato\n\n" COLOR_RESET);
    fflush(stdout);
}

void ui_show_my_board(const Client *client) {
    ui_clear_screen();
    ui_show_banner();
    print_grid(client->my_board, "  LA TUA FLOTTA", true);
}

void ui_prompt_login(char *username, size_t max_len) {
    printf(COLOR_CYAN "\n  Scegli il tuo nickname: " COLOR_RESET);
    fflush(stdout);

    if (fgets(username, max_len, stdin)) {
        size_t len = strlen(username);
        if (len > 0 && username[len - 1] == '\n') {
            username[len - 1] = '\0';
        }
    }
}

void ui_show_status(const char *message) {
    printf(COLOR_BLUE "  >> %s" COLOR_RESET "\n", message);
    fflush(stdout);
}

void ui_show_error(const char *message) {
    printf(COLOR_RED "  [ERRORE] %s" COLOR_RESET "\n", message);
    fflush(stdout);
}

void ui_show_success(const char *message) {
    printf(COLOR_GREEN "  [FATTO] %s" COLOR_RESET "\n", message);
    fflush(stdout);
}

void ui_read_command(char *buffer, size_t buf_size) {
    printf(COLOR_CYAN "> " COLOR_RESET);
    fflush(stdout);

    if (fgets(buffer, buf_size, stdin)) {
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
            buffer[len - 1] = '\0';
        }
    }
}

void ui_flush_input(void) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

int ui_wait_input(int seconds) {
    fd_set fds;
    struct timeval tv;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    tv.tv_sec = seconds;
    tv.tv_usec = 0;

    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
}

void ui_show_placement_instructions(int ship_size, int ship_num) {
    static const char* ship_names[] = {
        "Portaerei", "Corazzata", "Incrociatore", "Sottomarino", "Cacciatorpediniere"
    };

    printf("\n" COLOR_BOLD "~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~\n" COLOR_RESET);
    printf(COLOR_CYAN "           SISTEMAZIONE FLOTTA -- %d/5\n" COLOR_RESET, ship_num);
    printf(COLOR_BOLD "~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~\n\n" COLOR_RESET);
    printf("  Nave: " COLOR_YELLOW "%s" COLOR_RESET " (lunghezza: %d)\n\n",
           ship_names[ship_num - 1], ship_size);
    printf("  Formato: " COLOR_GREEN "riga_inizio,colonna_inizio,riga_fine,colonna_fine" COLOR_RESET "\n");
    printf("  Esempi:  " COLOR_WHITE "1,A,1,E" COLOR_RESET " (orizzontale) oppure " COLOR_WHITE "3,B,6,B" COLOR_RESET " (verticale)\n");
    printf("  Coordinate: 1-10 per le righe e A-J per le colonne.\n\n");
    fflush(stdout);
}

void ui_show_fire_instructions(void) {
    printf(COLOR_BOLD COLOR_GREEN "\n  >>> TOCCA A TE! <<<\n" COLOR_RESET);
    printf("  Dove vuoi sparare? (" COLOR_GREEN "riga,colonna" COLOR_RESET " es. 5,C) o 'quit':\n");
    fflush(stdout);
}

void ui_show_fire_result(const char *result, int row, int col) {
    if (strcmp(result, "hit") == 0) {
        printf(COLOR_RED COLOR_BOLD "  COLPITO!" COLOR_RESET " [%d,%c]\n", row + 1, 'A' + col);
    } else if (strcmp(result, "miss") == 0) {
        printf(COLOR_YELLOW "  MANCATO!" COLOR_RESET " [%d,%c]\n", row + 1, 'A' + col);
    } else if (strcmp(result, "sunk") == 0) {
        printf(COLOR_RED COLOR_BOLD "  AFFONDATA!" COLOR_RESET " [%d,%c]\n", row + 1, 'A' + col);
    }
    fflush(stdout);
}

void ui_show_game_list(const char *list) {
    printf("\n" COLOR_BOLD "~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~\n" COLOR_RESET);
    printf(COLOR_CYAN "                 SFIDE APERTE\n" COLOR_RESET);
    printf(COLOR_BOLD "~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~\n\n" COLOR_RESET);

    if (list == NULL || strlen(list) == 0) {
        printf("  " COLOR_YELLOW "Nessuna sfida aperta al momento.\n" COLOR_RESET);
    } else {
        char list_copy[1024];
        strncpy(list_copy, list, sizeof(list_copy) - 1);
        list_copy[sizeof(list_copy) - 1] = '\0';

        char *token = strtok(list_copy, ",");
        int num = 1;
        while (token != NULL) {
            char *colon = strchr(token, ':');
            if (colon) {
                *colon = '\0';
                printf("  %d) Codice: " COLOR_GREEN "%s" COLOR_RESET " - Sfidante: " COLOR_CYAN "%s" COLOR_RESET "\n",
                       num++, token, colon + 1);
            }
            token = strtok(NULL, ",");
        }
    }
    printf("\n" COLOR_BOLD "~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~\n" COLOR_RESET);
    fflush(stdout);
}

void ui_show_game_over(bool victory) {
    printf("\n" COLOR_BOLD "~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~\n" COLOR_RESET);
    if (victory) {
        printf(COLOR_GREEN COLOR_BOLD "              HAI VINTO! Ottima partita!         \n" COLOR_RESET);
    } else {
        printf(COLOR_RED COLOR_BOLD   "         HAI PERSO! Ritenta alla prossima         \n" COLOR_RESET);
    }
    printf(COLOR_BOLD "~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~\n\n" COLOR_RESET);
    fflush(stdout);
}

void ui_prompt_rematch(void) {
    printf("\n" COLOR_BOLD "~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~\n" COLOR_RESET);
    printf(COLOR_YELLOW "                UN'ALTRA SFIDA?                  \n" COLOR_RESET);
    printf(COLOR_BOLD "~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~\n\n" COLOR_RESET);
    printf("  Vuoi rigiocare con lo stesso avversario?\n");
    printf("  Digita " COLOR_GREEN "'y'" COLOR_RESET " per si' o " COLOR_RED "'n'" COLOR_RESET " per tornare alla lobby.\n\n");
    fflush(stdout);
}
