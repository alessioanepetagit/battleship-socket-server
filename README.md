# Sfida Navale - Progetto di Laboratorio di Sistemi Operativi

Server multi-client in C per giocare a Battaglia Navale, con client a riga di
comando. Comunicazione via **socket TCP** (niente websocket), un **thread per
client** lato server, memoria condivisa protetta da **mutex**, avvio tramite
**docker-compose**.

---

## 1. Struttura del progetto

```
BattagliaNavale_Rossi/
├── docker-compose.yml       # server + due client
├── Makefile                 # compilazione locale senza Docker
├── server/
│   ├── Dockerfile
│   └── src/
│       ├── main.c            # parsing argomenti, avvio
│       ├── server.c/.h       # socket, bind, listen, accept, segnali
│       ├── client_handler.c/.h # thread worker: un thread per client
│       ├── game_manager.c/.h   # tabelle condivise giocatori/partite + mutex
│       ├── game_logic.c/.h     # regole del gioco (griglia, navi, colpi)
│       └── protocol.c/.h       # formato dei messaggi, send/receive
└── client/
    ├── Dockerfile
    └── src/
        ├── main.c            # parsing argomenti / variabili d'ambiente
        ├── client.c/.h       # socket, thread ricevente, macchina a stati
        └── ui.c/.h           # interfaccia testuale (griglie, menu, colori)
```

---

## 2. Come si avvia

### Con Docker (modo previsto dalla traccia)

```bash
# 1. avvia il server in background
docker compose up -d --build server

# 2. in due terminali diversi, avvia i due client
docker compose run --rm client1
docker compose run --rm client2

# per fermare tutto
docker compose down
```

I client si collegano al server usando il nome di servizio `server` sulla rete
`battleship-net` creata da compose (le variabili `SERVER_HOST` e `SERVER_PORT`
sono impostate nel `docker-compose.yml`).

### In locale, senza Docker

```bash
make                 # compila server e client
./server/server -p 8080      # terminale 1
./client/client              # terminale 2  (default 127.0.0.1:8080)
./client/client              # terminale 3
```

---

## 3. Come si gioca

1. Ogni client sceglie un **nickname** (deve essere unico sul server).
2. Dalla lobby:
   - `1` crea una nuova sfida e stampa il **codice partita** (6 caratteri);
   - `2` elenca le sfide aperte;
   - `3` entra in una sfida inserendo il codice;
   - `q` esce dal gioco.
3. Chi ha creato la partita riceve la richiesta e risponde
   `accept <nickname>` oppure `reject <nickname>`.
4. **Fase di posizionamento**: cinque navi, in quest'ordine di lunghezza
   5, 4, 3, 3, 2. Formato `riga1,colonna1,riga2,colonna2`, ad esempio
   `1,A,1,E` (orizzontale) o `3,B,6,B` (verticale). Righe 1-10, colonne A-J.
   È il **server** a verificare bordi, allineamento, lunghezza e sovrapposizioni.
5. **Fase di combattimento**: si spara con `riga,colonna` (es. `5,C`).
   Il server alterna i turni e risponde `HIT`, `MISS` o `SUNK`.
6. **Fine partita**: chi affonda tutte le navi avversarie riceve `YOU_WIN`,
   l'altro `YOU_LOSE`. A entrambi viene chiesto se vogliono la rivincita:
   `y` per rigiocare con lo stesso avversario, `n` per tornare in lobby.
   La partita riparte solo se **entrambi** rispondono `y`.

---

## 4. Protocollo applicativo

Messaggi di testo terminati da `\n`, campi separati da `|`.

### Comandi (client -> server)

| Comando | Formato | Significato |
|---|---|---|
| LOGIN | `LOGIN\|nickname` | registra il nickname |
| CREATE_GAME | `CREATE_GAME` | crea una partita |
| LIST_GAMES | `LIST_GAMES` | elenca le partite in attesa |
| JOIN_GAME | `JOIN_GAME\|codice` | chiede di partecipare |
| ACCEPT_INVITE | `ACCEPT_INVITE\|nickname\|id` | il creatore accetta |
| REJECT_INVITE | `REJECT_INVITE\|nickname\|id` | il creatore rifiuta |
| PLACE_SHIP | `PLACE_SHIP\|r1\|c1\|r2\|c2` | posiziona una nave (indici 0-9) |
| READY | `READY` | flotta completata |
| FIRE | `FIRE\|riga\|colonna` | spara (indici 0-9) |
| LEAVE_GAME | `LEAVE_GAME` | esce dalla partita, resta connesso |
| REMATCH | `REMATCH` | chiede la rivincita |
| REMATCH_DECLINE | `REMATCH_DECLINE` | rifiuta la rivincita, torna in lobby |
| QUIT | `QUIT` | chiude la sessione |

### Risposte (server -> client)

`OK`, `ERROR|codice|descrizione`, `WELCOME|id`, `GAME_CREATED|codice`,
`GAME_LIST|cod:nick,cod:nick`, `JOIN_REQUEST|nick|id`, `JOIN_ACCEPTED|codice`,
`JOIN_REJECTED`, `SHIP_PLACED|n|r1|c1|r2|c2`, `INVALID_PLACEMENT|motivo`,
`GAME_START`, `YOUR_TURN`, `WAIT_TURN`, `HIT|r|c`, `MISS|r|c`, `SUNK|r|c|dim`,
`ENEMY_FIRE|r|c|esito`, `YOU_WIN`, `YOU_LOSE`, `OPPONENT_DISCONNECTED`,
`PLAY_AGAIN_PROMPT`, `REMATCH_REQUEST`, `REMATCH_REJECTED`, `BACK_TO_LOBBY`.

### Codici di errore principali

`100` comando non valido, `101` parametri non validi, `200` nickname occupato,
`201` login mancante, `300` partita non trovata, `301` partita piena,
`302` non è il tuo turno, `304` cella già colpita, `305` rivincita non
disponibile, `306` nessuna richiesta pendente, `307` non puoi unirti alla tua
partita, `308` azione non consentita nello stato attuale, `500` server pieno.

---

## 5. Scelte implementative (sistemi operativi)

- **Socket TCP**: `socket(AF_INET, SOCK_STREAM, 0)`, `bind()`, `listen()`,
  `accept()`, `read()`/`write()`. Sul client `inet_aton()` e, se l'host non è
  numerico (caso Docker), `gethostbyname()`.
- **Concorrenza**: dopo ogni `accept()` il server fa `pthread_create()` +
  `pthread_detach()`, così il thread libera da solo le proprie risorse.
- **Sincronizzazione**: un mutex globale per la tabella dei giocatori, uno per
  la tabella delle partite, più un mutex per ogni giocatore e per ogni partita.
  I mutex degli slot vengono creati una volta sola in `gm_init()` e distrutti
  solo in `gm_destroy()`: gli slot vengono riusati, mai "smontati" mentre un
  altro thread potrebbe averne il puntatore.
- **Prevenzione del deadlock**: quando servono due lock contemporaneamente
  (colpo sparato: board dell'attaccante + board del difensore) si acquisiscono
  sempre in **ordine canonico**, prima il giocatore con id minore. Così non può
  formarsi attesa circolare.
- **Segnali**: `SIGPIPE` ignorato (scrivere su un socket chiuso restituisce -1
  con `EPIPE` invece di uccidere il processo), `SIGINT`/`SIGTERM` intercettati
  con `sigaction()` per lo shutdown pulito (chiusura del socket di ascolto e
  deallocazione delle strutture).
- **Rilevamento disconnessioni**: `read()` che restituisce 0 significa EOF,
  cioè client scollegato: il server avvisa l'avversario, chiude la partita e
  libera lo slot.
- **Lato client**: un thread ricevente separato legge i messaggi asincroni
  (turni, colpi, inviti) mentre il thread principale legge l'input con
  `select()` e timeout di 100 ms, così un cambio di stato non lascia l'utente
  bloccato su un prompt vecchio. Le `write()` sul socket sono protette da un
  mutex perché entrambi i thread possono inviare.

---

## 6. Limiti noti

- Massimo 64 giocatori connessi e 32 partite contemporanee (`MAX_PLAYERS`,
  `MAX_GAMES`): oltre, il server risponde con l'errore 500.
- Le navi vanno posizionate nell'ordine fisso 5, 4, 3, 3, 2.
- Non è implementata la funzionalità opzionale "attendi che un altro
  giocatore si unisca dopo l'abbandono dell'avversario": alla caduta di un
  giocatore la partita viene chiusa e l'altro torna in lobby.
