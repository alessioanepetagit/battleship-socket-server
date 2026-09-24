#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
//static significa che l'array è privato di questo file
//const char* perchè è un puntatore a una stringa che non va modificata
static const char* COMMAND_STRINGS[] = {
    "LOGIN",
    "CREATE_GAME",
    "LIST_GAMES",
    "JOIN_GAME",
    "ACCEPT_INVITE",
    "REJECT_INVITE",
    "PLACE_SHIP",
    "READY",
    "FIRE",
    "LEAVE_GAME",
    "REMATCH",
    "REMATCH_DECLINE",
    "QUIT",
    "UNKNOWN"
};

static const char* RESPONSE_STRINGS[] = {
    "OK",
    "ERROR",
    "WELCOME",
    "GAME_CREATED",
    "GAME_LIST",
    "JOIN_REQUEST",
    "JOIN_ACCEPTED",
    "JOIN_REJECTED",
    "SHIP_PLACED",
    "INVALID_PLACEMENT",
    "GAME_START",
    "YOUR_TURN",
    "WAIT_TURN",
    "HIT",
    "MISS",
    "SUNK",
    "ENEMY_FIRE",
    "YOU_WIN",
    "YOU_LOSE",
    "OPPONENT_DISCONNECTED",
    "PLAY_AGAIN_PROMPT",
    "REMATCH_REQUEST",
    "REMATCH_REJECTED",
    "BACK_TO_LOBBY"
};

static const char* ERROR_STRINGS[] = {
    //designated initializer di C: 
    // [ERR_NONE] = "Nessun errore" significa metti questa
    //stringa nella posizione dell'array corrispondente a ERR_NONE
    [ERR_NONE] = "Nessun errore",
    [ERR_INVALID_COMMAND] = "Comando non valido",
    [ERR_INVALID_PARAMS] = "Parametri non validi",
    [ERR_USERNAME_TAKEN] = "Username gia' in uso",
    [ERR_NOT_LOGGED_IN] = "Devi effettuare il login",
    [ERR_GAME_NOT_FOUND] = "Partita non trovata",
    [ERR_GAME_FULL] = "Partita piena",
    [ERR_NOT_YOUR_TURN] = "Non e' il tuo turno",
    [ERR_INVALID_COORDS] = "Coordinate non valide",
    [ERR_ALREADY_FIRED] = "Hai gia' sparato in questa cella",
    [ERR_REMATCH_NOT_AVAILABLE] = "Rivincita non disponibile",
    [ERR_NO_PENDING_INVITE] = "Nessuna richiesta di partecipazione da quel giocatore",
    [ERR_CANNOT_JOIN_OWN_GAME] = "Non puoi unirti alla partita che hai creato",
    [ERR_WRONG_STATE] = "Azione non consentita nello stato attuale",
    [ERR_SHIP_OVERLAP] = "Le navi si sovrappongono",
    [ERR_SHIP_OUT_OF_BOUNDS] = "Nave fuori dalla griglia",
    [ERR_SHIP_INVALID_SIZE] = "Dimensione nave non valida",
    [ERR_ALL_SHIPS_PLACED] = "Tutte le navi sono gia' posizionate",
    [ERR_SERVER_FULL] = "Server pieno",
    [ERR_INTERNAL] = "Errore interno del server"
};
//Serve a fare "FIRE" -> CMD_FIRE ad esempio
static CommandType string_to_command(const char *str) {
    if (!str) return CMD_UNKNOWN;
    //i<cmd_unknown scorre tutti i comandi validi
    for (int i = 0; i < CMD_UNKNOWN; i++) {
        if (strcmp(str, COMMAND_STRINGS[i]) == 0) {
    //se trova una corrispondenza, ritorna il tipo di comando 
            return (CommandType)i;
        }
    }
    return CMD_UNKNOWN;
}
//parse_message crea 
//Prende stringa ricevuta dal cliente tramite socket e la trasforma in struct Message
//Tipo riceve FIRE|C5\n e lo trasforma in struct Message con 
// msg.type=CMD_FIRE , msg.param_count=2 , msg.params[0]="FIRE" , msg.params[1]="C5"
bool parse_message(const char *raw_message, Message *msg) {
    if (!raw_message || !msg) return false;

    strncpy(msg->raw, raw_message, MAX_MESSAGE_LEN - 1);
    msg->raw[MAX_MESSAGE_LEN - 1] = '\0';
    //Rimuovo caratteri \n e \r finali dalla stringa raw_message, se presenti.
    size_t len = strlen(msg->raw);
    while (len > 0 && (msg->raw[len - 1] == '\n' || msg->raw[len - 1] == '\r')) {
        msg->raw[len - 1] = '\0';
        len--;
    }
    //Se dopo aver tolto \n e \r la stringa è vuota, ritorna false e tipo CMD_UNKNOWN
    if (len == 0) {
        msg->type = CMD_UNKNOWN;
        msg->param_count = 0;
        return false;
    }
    //facciamo backup stringa in buffer_copy per non distruggere il contenuto originale di msg->raw con strtok_r
    char buffer_copy[MAX_MESSAGE_LEN];
    strncpy(buffer_copy, msg->raw, sizeof(buffer_copy) - 1);
    buffer_copy[sizeof(buffer_copy) - 1] = '\0';
    //Inizializzo param_count a 0 e uso strtok_r per dividere la stringa in token separati da "|"
    //esempio: "FIRE|C5" diventa token[0]="FIRE" e token[1]="C5"
    msg->param_count = 0;
    char *saveptr = NULL;
    char *token = strtok_r(buffer_copy, "|", &saveptr);
    //Salviamo i parametri nella struttura msg->params fino a MAX_PARAMS
    //params[0]="Fire" , params[1]="C5". Avremo msg.param_count=2
    while (token != NULL && msg->param_count < MAX_PARAMS) {
        strncpy(msg->params[msg->param_count], token, MAX_PARAM_LEN - 1);
        msg->params[msg->param_count][MAX_PARAM_LEN - 1] = '\0';
        msg->param_count++;
        token = strtok_r(NULL, "|", &saveptr);
    }

    if (msg->param_count == 0) {
        msg->type = CMD_UNKNOWN;
        return false;
    }
    //Il primo parametro è il comando, quindi lo trasformiamo in tipo CommandType
    //esempio: "FIRE" -> CMD_FIRE . Se il comando è valido(!=CMD_UNKNOWN), ritorna alla fine true
    msg->type = string_to_command(msg->params[0]);
    return (msg->type != CMD_UNKNOWN);
}

//build_response fa l'inverso di parse_message: prende tipo ResponseType e parametri
//  ricevuti dal server e li trasforma in stringa da inviare al client tramite socket
// ... significa che la funzione accetta un numero variabile di argomenti, qui sono i parametri da includere nella risposta.
int build_response(ResponseType response, char *buffer, size_t buf_size, int param_count, ...) {
    if (!buffer || buf_size == 0) return -1; //controlliamo che buffer esiste e abbia spazio

    int written = snprintf(buffer, buf_size, "%s", RESPONSE_STRINGS[response]);
    if (written < 0 || (size_t)written >= buf_size) return -1;

    va_list args;
    va_start(args, param_count);

    for (int i = 0; i < param_count; i++) {
        const char *param = va_arg(args, const char*);
        if (!param) param = "";
        int added = snprintf(buffer + written, buf_size - written, "|%s", param);
        if (added < 0 || (size_t)(written + added) >= buf_size) {
            va_end(args);
            return -1;
        }
        written += added;
    }
    va_end(args);

    if ((size_t)(written + 1) < buf_size) {
        buffer[written] = '\n';
        buffer[written + 1] = '\0';
        written++;
    } else {
        return -1;
    }

    return written;
}

int send_message(int socket_fd, const char *message) {
    if (socket_fd < 0 || !message) return -1;

    size_t len = strlen(message);
    size_t total_sent = 0;
//Usiamo ciclo per assicurarci che tutti i byte del messaggio siano inviati. Esempio:
//vogliamo mandare un messaggio lungo 100 byte, ma write() può inviare solo 50 byte alla 
//volta. Quindi facciamo più chiamate a write() finché non abbiamo inviato tutti i 100 byte.
    while (total_sent < len) {
        ssize_t sent = write(socket_fd, message + total_sent, len - total_sent);
        if (sent < 0) {
            if (errno == EINTR) continue;
        
            return -1;
        }
        if (sent == 0) return -1;
        total_sent += (size_t)sent;
    }

    return (int)total_sent;
}

int receive_message(int socket_fd, char *buffer, size_t buf_size) {
    if (socket_fd < 0 || !buffer || buf_size == 0) return -1;

    size_t total_read = 0;
//la read() legge un byte alla volta perchè il protocollo stabilisce che \n indica la fine
// del messaggio. Quindi continuiamo a leggere finché non troviamo \n o raggiungiamo 
// la dimensione massima del buffer.
    while (total_read < buf_size - 1) {
        ssize_t n = read(socket_fd, buffer + total_read, 1);
        if (n < 0) {
            if (errno == EINTR) continue; //EINTR: system_call interrotta da segnale, forziamo a continuare
            return -1;
        }
        if (n == 0) {
            /* read() == 0 => EOF: il client ha chiuso il canale */
            buffer[total_read] = '\0';
            return 0;
        }

        total_read++;
        if (buffer[total_read - 1] == '\n') {
            break;
        }
    }
//trasforma contenuto in una normale stringa C
    buffer[total_read] = '\0';
    return (int)total_read;
}
//contrario di string_to_command. Esempio trasforma CMD_FIRE in "FIRE"
const char* command_to_string(CommandType cmd) {
    if (cmd >= 0 && cmd <= CMD_UNKNOWN) {
        return COMMAND_STRINGS[cmd];
    }
    return "INVALID";
}
//stesso concetto. Trasforma RSP_GAME_START in "GAME_START"
const char* response_to_string(ResponseType rsp) {
    if (rsp >= 0 && rsp <= RSP_BACK_TO_LOBBY) {
        return RESPONSE_STRINGS[rsp];
    }
    return "INVALID";
}
//trasforma ERR_INVALID_COMMAND in "Comando non valido"
const char* error_to_string(ErrorCode err) {
    if (err >= 0 && err <= ERR_INTERNAL) {
        const char *str = ERROR_STRINGS[err];
        return str ? str : "Errore sconosciuto";
    }
    return "Errore sconosciuto";
}
