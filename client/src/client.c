#include "client.h"
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>      /* MODIFICA: gethostbyname per risolvere "server" in Docker */
#include <pthread.h>

static const int SHIP_SIZES[5] = {5, 4, 3, 3, 2};

static bool parse_coords_input(const char *input, int *r1, int *c1, int *r2, int *c2) {
    char col1_char, col2_char;
    if (sscanf(input, "%d,%c,%d,%c", r1, &col1_char, r2, &col2_char) != 4) {
        return false;
    }

    col1_char = (char)toupper((unsigned char)col1_char);
    col2_char = (char)toupper((unsigned char)col2_char);

    if (col1_char < 'A' || col1_char > 'J' || col2_char < 'A' || col2_char > 'J') {
        return false;
    }
    if (*r1 < 1 || *r1 > 10 || *r2 < 1 || *r2 > 10) {
        return false;
    }

    *r1 -= 1;
    *r2 -= 1;
    *c1 = col1_char - 'A';
    *c2 = col2_char - 'A';

    return true;
}

static bool parse_fire_input(const char *input, int *row, int *col) {
    char col_char;
    if (sscanf(input, "%d,%c", row, &col_char) != 2) {
        return false;
    }

    col_char = (char)toupper((unsigned char)col_char);
    if (col_char < 'A' || col_char > 'J') return false;
    if (*row < 1 || *row > 10) return false;

    *row -= 1;
    *col = col_char - 'A';

    return true;
}

static void update_my_board_with_ship(Client *client, int r1, int c1, int r2, int c2) {
    int min_r = (r1 < r2) ? r1 : r2;
    int max_r = (r1 > r2) ? r1 : r2;
    int min_c = (c1 < c2) ? c1 : c2;
    int max_c = (c1 > c2) ? c1 : c2;

    for (int r = min_r; r <= max_r; r++) {
        for (int c = min_c; c <= max_c; c++) {
            client->my_board[r][c] = 'S';
        }
    }
}

static void clear_boards(Client *client) {
    client->ships_placed = 0;
    for (int r = 0; r < GRID_SIZE; r++) {
        for (int c = 0; c < GRID_SIZE; c++) {
            client->my_board[r][c] = '.';
            client->enemy_board[r][c] = '.';
        }
    }
}

/*
 * ui_read_command() e' bloccante (fgets su stdin), ma lo stato del client
 * puo' cambiare in qualsiasi momento a causa dei messaggi che arrivano dal
 * receiver thread. Questa funzione aspetta l'input con un timeout breve
 * (100 ms) ricontrollando lo stato ad ogni giro: se lo stato cambia PRIMA
 * che l'utente prema invio, ritorniamo false senza leggere nulla, cosi' il
 * ciclo principale puo' ridispatchare subito al case corretto.
 */
static bool wait_for_command(Client *client, ClientState expected_state, char *buffer, size_t buf_size) {
    printf(COLOR_CYAN "> " COLOR_RESET);
    fflush(stdout);

    while (client->state == expected_state) {
        fd_set fds;
        struct timeval tv;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 100000;

        int ready = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
        if (ready > 0) {
            if (client->state != expected_state) return false;
            if (fgets(buffer, buf_size, stdin)) {
                size_t len = strlen(buffer);
                if (len > 0 && buffer[len - 1] == '\n') buffer[len - 1] = '\0';
            } else {
                buffer[0] = '\0';
            }
            return true;
        }
    }
    return false;
}

void client_init(Client *client) {
    client->socket_fd = -1;
    client->player_id = -1;
    memset(client->username, 0, sizeof(client->username));
    client->state = CLIENT_DISCONNECTED;
    client->ships_placed = 0;
    client->opponent_wants_rematch = false;
    memset(client->current_game_code, 0, sizeof(client->current_game_code));

    clear_boards(client);

    pthread_mutex_init(&client->ui_lock, NULL);
    pthread_mutex_init(&client->send_lock, NULL);
}

int client_connect(Client *client, const char *host, int port) {
    client->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client->socket_fd < 0) {
        perror("Errore creazione socket");
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    /*
     * MODIFICA: prima si usava solo inet_pton, che accetta esclusivamente
     * indirizzi numerici. Dentro docker-compose pero' il server si chiama
     * "server", quindi proviamo prima con inet_aton (indirizzo numerico) e
     * in caso di fallimento risolviamo il nome con gethostbyname().
     */
    if (inet_aton(host, &server_addr.sin_addr) == 0) {
        struct hostent *he = gethostbyname(host);
        if (!he || he->h_addrtype != AF_INET) {
            fprintf(stderr, "Impossibile risolvere l'host '%s'\n", host);
            close(client->socket_fd);
            client->socket_fd = -1;
            return -1;
        }
        memcpy(&server_addr.sin_addr, he->h_addr_list[0], sizeof(struct in_addr));
    }

    if (connect(client->socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Errore connessione");
        close(client->socket_fd);
        client->socket_fd = -1;
        return -1;
    }

    client->state = CLIENT_CONNECTED;
    return 0;
}

void client_disconnect(Client *client) {
    if (client->socket_fd >= 0) {
        close(client->socket_fd);
        client->socket_fd = -1;
    }
    client->state = CLIENT_DISCONNECTED;
}

int client_send(Client *client, const char *message) {
    if (client->socket_fd < 0) return -1;

    /*
     * Il thread principale (input utente) e il receiver thread (es. invio
     * automatico di READY dopo l'ultima nave) possono chiamare client_send
     * concorrentemente sullo stesso socket. Il lock rende ogni invio atomico.
     */
    pthread_mutex_lock(&client->send_lock);

    size_t len = strlen(message);
    size_t total = 0;

    while (total < len) {
        ssize_t n = write(client->socket_fd, message + total, len - total);
        if (n <= 0) {
            if (errno == EINTR) continue;
            pthread_mutex_unlock(&client->send_lock);
            return -1;
        }
        total += (size_t)n;
    }

    pthread_mutex_unlock(&client->send_lock);
    return (int)total;
}

int client_receive(Client *client, char *buffer, size_t buf_size) {
    if (client->socket_fd < 0) return -1;

    size_t total = 0;
    while (total < buf_size - 1) {
        ssize_t n = read(client->socket_fd, buffer + total, 1);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return 0;   /* EOF: server chiuso */

        total++;
        if (buffer[total - 1] == '\n') break;
    }

    buffer[total] = '\0';
    return (int)total;
}

static void handle_response(Client *client, const char *response) {
    char resp_copy[MAX_MESSAGE_LEN];
    strncpy(resp_copy, response, sizeof(resp_copy) - 1);
    resp_copy[sizeof(resp_copy) - 1] = '\0';

    size_t len = strlen(resp_copy);
    if (len > 0 && resp_copy[len - 1] == '\n') resp_copy[len - 1] = '\0';

    char *saveptr = NULL;
    char *cmd = strtok_r(resp_copy, "|", &saveptr);
    if (!cmd) return;

    pthread_mutex_lock(&client->ui_lock);

    if (strcmp(cmd, "WELCOME") == 0) {
        char *id = strtok_r(NULL, "|", &saveptr);
        if (id) {
            client->player_id = atoi(id);
            client->state = CLIENT_IN_LOBBY;
            ui_clear_screen();
            ui_show_banner();
            ui_show_success("Accesso effettuato, buona partita!");
            ui_show_lobby_menu();
        }
    } else if (strcmp(cmd, "ERROR") == 0) {
        char *code = strtok_r(NULL, "|", &saveptr);
        char *msg  = strtok_r(NULL, "|", &saveptr);
        (void)code;
        ui_show_error(msg ? msg : "Errore sconosciuto");

        if (client->state == CLIENT_WAITING_LOGIN) {
            client->state = CLIENT_CONNECTED;

        } else if (client->state == CLIENT_IN_LOBBY ||
                   client->state == CLIENT_WAITING_OPPONENT) {
            ui_show_lobby_menu();

        } else if (client->state == CLIENT_PLACING_SHIPS ||
                   client->state == CLIENT_MY_TURN) {
            printf("  Riprova > ");
            fflush(stdout);

        } else if (client->state == CLIENT_WAITING_SERVER ||
                   client->state == CLIENT_GAME_OVER) {
            /* MODIFICA: se la rivincita non e' possibile torniamo in lobby
               invece di restare appesi come succedeva prima */
            client->state = CLIENT_IN_LOBBY;
            ui_show_lobby_menu();
        }
    } else if (strcmp(cmd, "OK") == 0) {
        char *msg = strtok_r(NULL, "|", &saveptr);
        if (msg) ui_show_status(msg);
    } else if (strcmp(cmd, "GAME_CREATED") == 0) {
        char *code = strtok_r(NULL, "|", &saveptr);
        if (code) {
            strncpy(client->current_game_code, code, sizeof(client->current_game_code) - 1);
            client->state = CLIENT_WAITING_OPPONENT;
            ui_clear_screen();
            ui_show_banner();
            ui_show_success("Sfida creata!");
            printf("  Codice sfida: " COLOR_GREEN COLOR_BOLD "%s" COLOR_RESET "\n", code);
            printf("  In attesa che qualcuno si unisca...\n");
            printf(COLOR_GRAY "  (Digita 'q' per annullare e tornare in lobby)\n\n" COLOR_RESET);
            fflush(stdout);
        }
    } else if (strcmp(cmd, "GAME_LIST") == 0) {
        char *list = strtok_r(NULL, "|", &saveptr);
        if (!list || strlen(list) == 0) {
            ui_show_status("Nessuna sfida aperta al momento. Creane una con '1'!");
        } else {
            ui_show_game_list(list);
        }
        ui_show_lobby_menu();
    } else if (strcmp(cmd, "JOIN_REQUEST") == 0) {
        char *username  = strtok_r(NULL, "|", &saveptr);
        char *player_id = strtok_r(NULL, "|", &saveptr);
        (void)player_id;
        const char *uname = username ? username : "Sconosciuto";
        printf("\n" COLOR_YELLOW "  [SFIDANTE] '%s' vuole giocare con te!\n" COLOR_RESET, uname);
        printf("  Digita " COLOR_GREEN "accept %s" COLOR_RESET " per accettare\n", uname);
        printf("  Digita " COLOR_RED   "reject %s" COLOR_RESET " per rifiutare\n> ", uname);
        fflush(stdout);
    } else if (strcmp(cmd, "JOIN_ACCEPTED") == 0) {
        char *code = strtok_r(NULL, "|", &saveptr);
        if (code) {
            strncpy(client->current_game_code, code, sizeof(client->current_game_code) - 1);
            ui_show_success("Sei dentro! La partita sta per iniziare...");
            client->state = CLIENT_WAITING_START;
        }
    } else if (strcmp(cmd, "JOIN_REJECTED") == 0) {
        ui_show_error("La tua richiesta e' stata rifiutata");
        client->state = CLIENT_IN_LOBBY;
        ui_show_lobby_menu();
    } else if (strcmp(cmd, "GAME_START") == 0) {
        /* vale sia per la prima partita sia per la rivincita */
        clear_boards(client);
        client->opponent_wants_rematch = false;
        client->state = CLIENT_PLACING_SHIPS;
        ui_clear_screen();
        ui_show_banner();
        ui_show_status("La partita inizia! Posiziona le tue navi.");
        ui_show_my_board(client);
        ui_show_placement_instructions(SHIP_SIZES[0], 1);
        printf("> ");
        fflush(stdout);
    } else if (strcmp(cmd, "SHIP_PLACED") == 0) {
        char *num    = strtok_r(NULL, "|", &saveptr);
        char *r1_str = strtok_r(NULL, "|", &saveptr);
        char *c1_str = strtok_r(NULL, "|", &saveptr);
        char *r2_str = strtok_r(NULL, "|", &saveptr);
        char *c2_str = strtok_r(NULL, "|", &saveptr);
        if (num) {
            client->ships_placed = atoi(num);
            /* la board locale si aggiorna solo dopo la conferma del server */
            if (r1_str && c1_str && r2_str && c2_str) {
                update_my_board_with_ship(client,
                    atoi(r1_str), atoi(c1_str),
                    atoi(r2_str), atoi(c2_str));
            }
            ui_clear_screen();
            ui_show_banner();
            ui_show_success("Nave posizionata!");
            ui_show_my_board(client);

            if (client->ships_placed < 5) {
                ui_show_placement_instructions(SHIP_SIZES[client->ships_placed], client->ships_placed + 1);
                printf("> ");
                fflush(stdout);
            } else {
                client->state = CLIENT_WAITING_START;
                client_send(client, "READY\n");
                ui_show_status("Flotta pronta! In attesa dell'avversario...");
                printf(COLOR_GRAY "  Non manca molto, tieniti pronto!\n" COLOR_RESET);
                fflush(stdout);
            }
        }
    } else if (strcmp(cmd, "INVALID_PLACEMENT") == 0) {
        char *reason = strtok_r(NULL, "|", &saveptr);
        ui_show_error(reason ? reason : "Posizionamento non valido");
        printf("  Riprova > ");
        fflush(stdout);
    } else if (strcmp(cmd, "YOUR_TURN") == 0) {
        client->state = CLIENT_MY_TURN;
        ui_show_boards(client);
        ui_show_fire_instructions();
        printf("> ");
        fflush(stdout);
    } else if (strcmp(cmd, "WAIT_TURN") == 0) {
        client->state = CLIENT_WAITING_TURN;
        ui_show_boards(client);
        printf("\n" COLOR_YELLOW "  >> Turno dell'avversario, aspetta la tua occasione! <<\n" COLOR_RESET);
        fflush(stdout);
    } else if (strcmp(cmd, "HIT") == 0 || strcmp(cmd, "SUNK") == 0) {
        char *row_str = strtok_r(NULL, "|", &saveptr);
        char *col_str = strtok_r(NULL, "|", &saveptr);
        if (row_str && col_str) {
            int row = atoi(row_str);
            int col = atoi(col_str);
            client->enemy_board[row][col] = 'H';
            ui_show_boards(client);
            ui_show_fire_result(strcmp(cmd, "SUNK") == 0 ? "sunk" : "hit", row, col);
        }
    } else if (strcmp(cmd, "MISS") == 0) {
        char *row_str = strtok_r(NULL, "|", &saveptr);
        char *col_str = strtok_r(NULL, "|", &saveptr);
        if (row_str && col_str) {
            int row = atoi(row_str);
            int col = atoi(col_str);
            client->enemy_board[row][col] = 'M';
            ui_show_boards(client);
            ui_show_fire_result("miss", row, col);
        }
    } else if (strcmp(cmd, "ENEMY_FIRE") == 0) {
        char *row_str = strtok_r(NULL, "|", &saveptr);
        char *col_str = strtok_r(NULL, "|", &saveptr);
        char *result = strtok_r(NULL, "|", &saveptr);
        if (row_str && col_str && result) {
            int row = atoi(row_str);
            int col = atoi(col_str);
            if (strcmp(result, "HIT") == 0 || strcmp(result, "SUNK") == 0) {
                client->my_board[row][col] = 'H';
            } else {
                client->my_board[row][col] = 'M';
            }
        }
    } else if (strcmp(cmd, "YOU_WIN") == 0) {
        ui_show_boards(client);
        ui_show_game_over(true);
    } else if (strcmp(cmd, "YOU_LOSE") == 0) {
        ui_show_boards(client);
        ui_show_game_over(false);
    } else if (strcmp(cmd, "PLAY_AGAIN_PROMPT") == 0) {
        /* MODIFICA: e' il server a chiedere se si vuole rigiocare */
        client->state = CLIENT_GAME_OVER;
        ui_prompt_rematch();
        printf("> ");
        fflush(stdout);
    } else if (strcmp(cmd, "REMATCH_REQUEST") == 0) {
        /* MODIFICA: l'avversario ha gia' accettato, lo segnaliamo */
        client->opponent_wants_rematch = true;
        printf("\n" COLOR_YELLOW "  [RIVINCITA] L'avversario vuole rigiocare! Rispondi 'y' o 'n'.\n" COLOR_RESET);
        printf("> ");
        fflush(stdout);
    } else if (strcmp(cmd, "REMATCH_REJECTED") == 0) {
        ui_show_error("L'avversario non vuole la rivincita.");
    } else if (strcmp(cmd, "BACK_TO_LOBBY") == 0) {
        /* MODIFICA: il server conferma che la partita e' chiusa e che
           siamo di nuovo liberi di crearne o cercarne un'altra */
        client->opponent_wants_rematch = false;
        clear_boards(client);
        memset(client->current_game_code, 0, sizeof(client->current_game_code));
        client->state = CLIENT_IN_LOBBY;
        ui_show_lobby_menu();
    } else if (strcmp(cmd, "OPPONENT_DISCONNECTED") == 0) {
        ui_show_error("Il tuo avversario ha lasciato la partita!");
    }

    pthread_mutex_unlock(&client->ui_lock);
}

typedef struct {
    Client *client;
    volatile bool *running;
} ReceiverArgs;

static void* receiver_thread(void *arg) {
    ReceiverArgs *args = (ReceiverArgs*)arg;
    Client *client = args->client;
    char buffer[MAX_MESSAGE_LEN];

    while (*args->running) {
        int n = client_receive(client, buffer, sizeof(buffer));
        if (n <= 0) {
            if (*args->running) {
                pthread_mutex_lock(&client->ui_lock);
                ui_show_error("Connessione persa con il server");
                pthread_mutex_unlock(&client->ui_lock);
            }
            client->state = CLIENT_DISCONNECTED;
            break;
        }
        handle_response(client, buffer);
    }
    return NULL;
}

void client_run(Client *client) {
    volatile bool running = true;
    ReceiverArgs recv_args;
    recv_args.client = client;
    recv_args.running = &running;
    pthread_t recv_thread;

    if (pthread_create(&recv_thread, NULL, receiver_thread, &recv_args) != 0) {
        ui_show_error("Impossibile avviare la connessione");
        return;
    }

    ui_clear_screen();
    ui_show_banner();

    char input[256];

    while (running && client->state != CLIENT_DISCONNECTED) {
        switch (client->state) {
            case CLIENT_CONNECTED: {
                char username[MAX_USERNAME];
                ui_prompt_login(username, sizeof(username));

                char login_msg[MAX_MESSAGE_LEN];
                snprintf(login_msg, sizeof(login_msg), "LOGIN|%s\n", username);
                ui_show_status("Mi collego al server...");

                if (client_send(client, login_msg) < 0) {
                    ui_show_error("Errore durante il login");
                }
                strncpy(client->username, username, sizeof(client->username) - 1);
                client->state = CLIENT_WAITING_LOGIN;
                break;
            }

            case CLIENT_WAITING_LOGIN:
                usleep(50000);
                break;

            case CLIENT_WAITING_OPPONENT:
            case CLIENT_IN_LOBBY: {
                ClientState state_before = client->state;
                if (!wait_for_command(client, state_before, input, sizeof(input))) {
                    continue;
                }

                if (strcmp(input, "1") == 0) {
                    client_send(client, "CREATE_GAME\n");
                } else if (strcmp(input, "2") == 0) {
                    client_send(client, "LIST_GAMES\n");
                } else if (strcmp(input, "3") == 0) {
                    printf("  Inserisci il codice della sfida: ");
                    fflush(stdout);
                    char code[16];
                    if (fgets(code, sizeof(code), stdin)) {
                        code[strcspn(code, "\r\n")] = '\0';
                        if (strlen(code) == 0) {
                            ui_show_error("Codice non inserito. Riprova.");
                            ui_show_lobby_menu();
                        } else {
                            char msg[MAX_MESSAGE_LEN];
                            snprintf(msg, sizeof(msg), "JOIN_GAME|%s\n", code);
                            client_send(client, msg);
                            ui_show_status("Richiesta inviata, aspetto una risposta...");
                        }
                    }
                } else if (strncmp(input, "accept ", 7) == 0) {
                    const char *target = input + 7;
                    if (strlen(target) == 0) {
                        ui_show_error("Specifica chi accettare. Esempio: accept Alessio");
                    } else {
                        char msg[MAX_MESSAGE_LEN];
                        snprintf(msg, sizeof(msg), "ACCEPT_INVITE|%s\n", target);
                        client_send(client, msg);
                        ui_show_status("Accettato!");
                    }
                } else if (strncmp(input, "reject ", 7) == 0) {
                    const char *target = input + 7;
                    if (strlen(target) == 0) {
                        ui_show_error("Specifica chi rifiutare. Esempio: reject Alessio");
                    } else {
                        char msg[MAX_MESSAGE_LEN];
                        snprintf(msg, sizeof(msg), "REJECT_INVITE|%s\n", target);
                        client_send(client, msg);
                    }
                } else if (strcmp(input, "q") == 0 || strcmp(input, "quit") == 0) {
                    if (client->state == CLIENT_WAITING_OPPONENT) {
                        /*
                         * MODIFICA: LEAVE_GAME al posto di QUIT_GAME.
                         * Prima il server chiudeva il socket mentre il client
                         * mostrava allegramente il menu della lobby.
                         * Ora aspettiamo BACK_TO_LOBBY dal server.
                         */
                        client_send(client, "LEAVE_GAME\n");
                        client->state = CLIENT_WAITING_SERVER;
                    } else {
                        client_send(client, "QUIT\n");
                        running = false;
                    }
                } else if (strlen(input) > 0) {
                    pthread_mutex_lock(&client->ui_lock);
                    ui_show_error("Comando non riconosciuto. Scegli un'opzione dal menu.");
                    ui_show_lobby_menu();
                    pthread_mutex_unlock(&client->ui_lock);
                }
                break;
            }

            case CLIENT_PLACING_SHIPS: {
                int r1, c1, r2, c2;
                do {
                    if (!wait_for_command(client, CLIENT_PLACING_SHIPS, input, sizeof(input))) break;
                    if (strlen(input) == 0) continue;

                    /* MODIFICA: si puo' abbandonare anche durante il posizionamento */
                    if (strcmp(input, "quit") == 0) {
                        client_send(client, "LEAVE_GAME\n");
                        client->state = CLIENT_WAITING_SERVER;
                        break;
                    }

                    if (parse_coords_input(input, &r1, &c1, &r2, &c2)) {
                        char msg[MAX_MESSAGE_LEN];
                        snprintf(msg, sizeof(msg), "PLACE_SHIP|%d|%d|%d|%d\n", r1, c1, r2, c2);
                        client_send(client, msg);
                        break; /* aspettiamo la risposta del server */
                    } else {
                        pthread_mutex_lock(&client->ui_lock);
                        ui_show_error("Formato non valido! Usa: riga1,col1,riga2,col2 (es. 1,A,1,E)");
                        printf("  Riprova > ");
                        fflush(stdout);
                        pthread_mutex_unlock(&client->ui_lock);
                    }
                } while (client->state == CLIENT_PLACING_SHIPS);
                break;
            }

            case CLIENT_WAITING_START:
            case CLIENT_WAITING_TURN:
            case CLIENT_WAITING_SERVER:
                usleep(50000);
                break;

            case CLIENT_MY_TURN:
                if (!wait_for_command(client, CLIENT_MY_TURN, input, sizeof(input))) continue;

                if (strcmp(input, "quit") == 0) {
                    client_send(client, "LEAVE_GAME\n");   /* MODIFICA */
                    client->state = CLIENT_WAITING_SERVER;
                } else {
                    int row, col;
                    if (parse_fire_input(input, &row, &col)) {
                        char msg[MAX_MESSAGE_LEN];
                        snprintf(msg, sizeof(msg), "FIRE|%d|%d\n", row, col);
                        client_send(client, msg);
                    } else {
                        pthread_mutex_lock(&client->ui_lock);
                        ui_show_error("Formato non valido! Usa: riga,colonna (es. 5,C)");
                        pthread_mutex_unlock(&client->ui_lock);
                    }
                }
                break;

            case CLIENT_GAME_OVER:
                if (!wait_for_command(client, CLIENT_GAME_OVER, input, sizeof(input))) continue;

                if (strcmp(input, "y") == 0 || strcmp(input, "yes") == 0) {
                    /* MODIFICA: ora il server gestisce davvero REMATCH */
                    client_send(client, "REMATCH\n");
                    ui_show_status("Richiesta di rivincita inviata...");
                    client->state = CLIENT_WAITING_SERVER;
                } else if (strcmp(input, "n") == 0 || strcmp(input, "no") == 0) {
                    /* MODIFICA: REMATCH_DECLINE chiude la partita ma NON la
                       connessione: si torna in lobby e si puo' ricominciare */
                    client_send(client, "REMATCH_DECLINE\n");
                    client->state = CLIENT_WAITING_SERVER;
                } else if (strlen(input) > 0) {
                    pthread_mutex_lock(&client->ui_lock);
                    ui_show_error("Risposta non valida. Digita 'y' per rivincita o 'n' per tornare in lobby.");
                    ui_prompt_rematch();
                    printf("> ");
                    fflush(stdout);
                    pthread_mutex_unlock(&client->ui_lock);
                }
                break;

            default:
                usleep(50000);
                break;
        }
    }

    running = false;
    client_disconnect(client);
    pthread_join(recv_thread, NULL);

    /* MODIFICA: i mutex si distruggono qui, dopo la join del receiver
       thread (prima client_disconnect li distruggeva mentre il receiver
       thread poteva ancora usarli) */
    pthread_mutex_destroy(&client->ui_lock);
    pthread_mutex_destroy(&client->send_lock);

    printf("\n  Arrivederci!\n\n");
}
