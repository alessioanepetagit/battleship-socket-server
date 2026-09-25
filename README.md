# 🚢 Sfida Navale - Sistema Client-Server

Progetto per il corso di Laboratorio di Sistemi Operativi.

## 📋 Descrizione

Sistema multiplayer per il gioco Battaglia Navale, composto da:
- **Server** in C: gestisce più partite in parallelo, un thread per client, sincronizzazione con mutex, validazione delle mosse lato server
- **Client CLI** in C: interfaccia a riga di comando con griglie colorate

Client e server comunicano tramite **socket TCP** (nessuna websocket), con un protocollo testuale a righe (`COMANDO|PARAM1|PARAM2|...\n`).

## 🚀 Avvio Rapido

### Con Docker Compose (modalità consigliata)

```bash
# 1. Avvia il server in background
docker compose up -d --build server

# 2. In due terminali diversi, avvia i due client
docker compose run --rm client1
docker compose run --rm client2

# per fermare tutto
docker compose down
```

I client si collegano al server usando il nome del servizio `server` sulla rete
`battleship-net` creata da Compose (variabili d'ambiente `SERVER_HOST` e
`SERVER_PORT`, già impostate nel `docker-compose.yml`).

### In locale, senza Docker

```bash
make                          # compila server e client
./server/server -p 8080       # terminale 1
./client/client                # terminale 2  (default 127.0.0.1:8080)
./client/client                # terminale 3, per il secondo giocatore
```

Oppure specificando host e porta esplicitamente:
```bash
./client/client -h 127.0.0.1 -p 8080
```

## 🎮 Come Giocare

1. **Login**: scegli un nickname (deve essere unico tra i giocatori connessi)
2. **Lobby**:
   - `1` crea una nuova sfida e mostra il codice partita (6 caratteri)
   - `2` mostra le sfide aperte in attesa di un avversario
   - `3` entra in una sfida inserendo il codice
   - `q` esce dal gioco
3. Chi ha creato la partita riceve la richiesta di un altro giocatore e risponde
   `accept <nickname>` per accettarla oppure `reject <nickname>` per rifiutarla
4. **Posizionamento navi**: 5 navi da posizionare in ordine, lunghezze 5, 4, 3, 3, 2.
   Formato `riga_inizio,colonna_inizio,riga_fine,colonna_fine`, ad esempio
   `1,A,1,E` (orizzontale) o `3,B,6,B` (verticale). Righe 1-10, colonne A-J.
   È il **server** a validare bordi, allineamento, lunghezza e sovrapposizioni.
5. **Combattimento**: si spara con `riga,colonna` (es. `5,C`). Il server alterna
   i turni e risponde con `COLPITO`, `MANCATO` o `AFFONDATA`.
6. **Fine partita**: chi affonda tutte le navi avversarie vince, l'altro perde.
   A entrambi viene chiesto se vogliono la rivincita: `y` per rigiocare con lo
   stesso avversario, `n` per tornare in lobby. La partita riparte solo se
   **entrambi** rispondono `y`.


## 📁 Struttura Progetto

```
battleship-socket-server/
├── server/                     # Server C
│   ├── src/
│   │   ├── main.c              # Entry point server, parsing argomenti
│   │   ├── server.c/.h         # Socket, bind, listen, accept, segnali
│   │   ├── client_handler.c/.h # Thread worker: un thread per client
│   │   ├── game_manager.c/.h   # Tabelle condivise giocatori/partite + mutex
│   │   ├── game_logic.c/.h     # Regole del gioco (griglia, navi, colpi)
│   │   └── protocol.c/.h       # Formato dei messaggi, send/receive
│   └── Dockerfile
├── client/                     # Client CLI in C
│   ├── src/
│   │   ├── main.c              # Entry point client, parsing argomenti/env
│   │   ├── client.c/.h         # Socket, thread ricevente, macchina a stati
│   │   └── ui.c/.h             # Interfaccia testuale (griglie, menu, colori)
│   └── Dockerfile
├── Makefile                    # Compilazione locale senza Docker
├── docker-compose.yml          # server + due client
└── README.md
```

## 🔧 Opzioni Linea di Comando

### Server
```
./server [opzioni]
  -p, --port PORT    Porta di ascolto (default: 8080)
  --help             Mostra aiuto
```

### Client
```
./client [opzioni]
  -h, --host HOST    Indirizzo o nome del server (default: 127.0.0.1)
  -p, --port PORT    Porta del server (default: 8080)
  --help             Mostra aiuto
```
In alternativa, host e porta possono essere impostati con le variabili
d'ambiente `SERVER_HOST` e `SERVER_PORT` (usate da Docker Compose).

## 🔌 Protocollo di Comunicazione

Messaggi testuali terminati da `\n`, campi separati da `|`: `COMANDO|PARAM1|PARAM2|...\n`

### Comandi (client → server)
| Comando | Formato | Descrizione |
|---------|---------|-------------|
| LOGIN | `LOGIN\|nickname` | Registra il nickname |
| CREATE_GAME | `CREATE_GAME` | Crea una nuova partita |
| LIST_GAMES | `LIST_GAMES` | Elenca le partite in attesa |
| JOIN_GAME | `JOIN_GAME\|codice` | Richiede di partecipare a una partita |
| ACCEPT_INVITE | `ACCEPT_INVITE\|nickname` | Il creatore accetta la richiesta |
| REJECT_INVITE | `REJECT_INVITE\|nickname` | Il creatore rifiuta la richiesta |
| PLACE_SHIP | `PLACE_SHIP\|r1\|c1\|r2\|c2` | Posiziona una nave (indici 0-9) |
| READY | `READY` | Segnala che la flotta è completa |
| FIRE | `FIRE\|riga\|colonna` | Spara (indici 0-9) |
| LEAVE_GAME | `LEAVE_GAME` | Esce dalla partita, resta connesso in lobby |
| REMATCH | `REMATCH` | Chiede la rivincita |
| REMATCH_DECLINE | `REMATCH_DECLINE` | Rifiuta la rivincita, torna in lobby |
| QUIT | `QUIT` | Chiude la sessione |

### Risposte principali (server → client)
`OK`, `ERROR|codice|descrizione`, `WELCOME|id`, `GAME_CREATED|codice`,
`GAME_LIST|cod:nick,cod:nick`, `JOIN_REQUEST|nick|id`, `JOIN_ACCEPTED|codice`,
`JOIN_REJECTED`, `SHIP_PLACED|n|r1|c1|r2|c2`, `INVALID_PLACEMENT|motivo`,
`GAME_START`, `YOUR_TURN`, `WAIT_TURN`, `HIT|r|c`, `MISS|r|c`, `SUNK|r|c|dim`,
`ENEMY_FIRE|r|c|esito`, `YOU_WIN`, `YOU_LOSE`, `OPPONENT_DISCONNECTED`,
`PLAY_AGAIN_PROMPT`, `REMATCH_REQUEST`, `REMATCH_REJECTED`, `BACK_TO_LOBBY`.

## ⚙️ Caratteristiche Tecniche

- **Concorrenza**: un thread POSIX per ogni client (`pthread_create` +
  `pthread_detach`), così il sistema regge più partite/client simultanei
- **Sincronizzazione**: mutex separati per la tabella giocatori, per la tabella
  partite, e uno per ogni singolo giocatore/partita, per non serializzare
  inutilmente client che non stanno interagendo tra loro
- **Prevenzione deadlock**: quando un'operazione richiede il lock di due
  giocatori insieme (es. il colpo sparato, che tocca sia l'attaccante che il
  difensore), i lock vengono presi sempre in ordine canonico (prima l'id più
  basso), così non si può formare un'attesa circolare
- **Gestione segnali**: `SIGINT`/`SIGTERM` intercettati per uno shutdown
  pulito, `SIGPIPE` ignorato così una scrittura su un socket già chiuso non
  termina il processo
- **Rilevamento disconnessioni**: una `read()` che ritorna 0 viene trattata
  come disconnessione del client: l'avversario viene avvisato, la partita
  chiusa e lo slot liberato
- **Limiti**: massimo 64 giocatori connessi e 32 partite contemporanee
  (oltre questa soglia il server risponde con l'errore "server pieno")

## 📝 Author

  Alessio Anepeta : https://github.com/alessioanepetagit/alessioanepetagit