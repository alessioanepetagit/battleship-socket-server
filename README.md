# 🚢 Battaglia Navale - Sistema Client-Server

Progetto per il corso di Laboratorio di Sistemi Operativi.

## 📋 Descrizione

Sistema multiplayer per il gioco Battaglia Navale, composto da:
- **Server** in C: gestisce più partite in parallelo, un thread per client, sincronizzazione con mutex, validazione di navi e mosse lato server
- **Client CLI** in C: interfaccia a riga di comando con griglie colorate

Client e server comunicano tramite **socket TCP** (nessuna websocket), con un protocollo testuale a righe (`COMANDO|PARAM1|PARAM2|...\n`).

Ogni partita ha due giocatori. Chi la crea ne riceve un codice univoco, che la rende visibile agli altri client; il creatore accetta o rifiuta chi chiede di unirsi. Il server gestisce sia il posizionamento delle navi sia il combattimento, e a fine partita permette la rivincita con lo stesso avversario o il ritorno in lobby. Se un giocatore cade, chi resta può terminare la partita oppure aspettare un nuovo avversario.

## 🚀 Avvio Rapido

### Con Docker Compose (modalità consigliata)

```bash
# 1. Costruisci e avvia il server in background
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

Dopo aver modificato il codice, ricostruisci le immagini con `docker compose build`.


Scorciatoie del `Makefile`: `make run-server`, `make run-client`, `make docker-up`, `make docker-down`, `make clean`.

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
7. **Avversario caduto** (opzionale): se l'avversario si disconnette o abbandona
   in qualsiasi momento dopo l'inizio della partita, chi resta sceglie:
   - `w` **aspetta** un nuovo giocatore: la stessa partita (stesso codice) torna
     visibile tra le sfide aperte e il giocatore rimasto ne è il creatore, quindi
     accetta o rifiuta chi chiede di unirsi
   - `e` **termina** la partita e torna in lobby

   Finché non sceglie, la partita è chiusa: non compare nell'elenco e nessuno può
   unirsi. Il rifiuto della rivincita (punto 6) resta invece un'uscita
   definitiva: entrambi tornano in lobby.

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
| LEAVE_GAME | `LEAVE_GAME` | Esce dalla partita, resta connesso in lobby; dopo la caduta dell'avversario termina la partita |
| WAIT_NEW_PLAYER | `WAIT_NEW_PLAYER` | Dopo la caduta dell'avversario: riapre la partita e aspetta un nuovo giocatore |
| REMATCH | `REMATCH` | Chiede la rivincita |
| REMATCH_DECLINE | `REMATCH_DECLINE` | Rifiuta la rivincita, torna in lobby |
| QUIT | `QUIT` | Chiude la sessione |

### Risposte principali (server → client)
`OK`, `ERROR|codice|descrizione`, `WELCOME|id`, `GAME_CREATED|codice`,
`GAME_LIST|cod:nick,cod:nick`, `JOIN_REQUEST|nick|id`, `JOIN_ACCEPTED|codice`,
`JOIN_REJECTED`, `SHIP_PLACED|n|r1|c1|r2|c2`, `INVALID_PLACEMENT|motivo`,
`GAME_START`, `YOUR_TURN`, `WAIT_TURN`, `HIT|r|c`, `MISS|r|c`, `SUNK|r|c|dim`,
`ENEMY_FIRE|r|c|esito`, `YOU_WIN`, `YOU_LOSE`, `OPPONENT_DISCONNECTED`,
`WAITING_NEW_PLAYER|codice`, `PLAY_AGAIN_PROMPT`, `REMATCH_REQUEST`,
`REMATCH_REJECTED`, `BACK_TO_LOBBY`.

## ⚙️ Caratteristiche Tecniche

### Concorrenza e sincronizzazione

- **Un thread per client**: il thread principale fa solo `accept()`; ogni
  connessione è servita da un thread POSIX dedicato (`pthread_create` +
  `pthread_detach`), così più partite e più client procedono in parallelo e un
  client lento non blocca gli altri
- **Mutex a più livelli**: uno per la tabella giocatori (`players_lock`), uno per
  la tabella partite (`games_lock`) e uno per ogni singolo giocatore e ogni
  singola partita. Le tabelle servono solo a trovare/allocare/liberare gli slot;
  lo stato di gioco è protetto dal lock del singolo oggetto, per non serializzare
  client che non interagiscono tra loro
- **Niente lock annidati, tranne uno**: i lock vengono presi uno alla volta e
  rilasciati prima di prenderne un altro. L'unica eccezione è il colpo (`FIRE`),
  che tocca attaccante e difensore: i due lock vengono presi sempre in **ordine
  canonico** (prima l'id più basso), quindi non può formarsi un'attesa circolare
  e due giocatori che sparano insieme non vanno in deadlock
- **Nessuna `send()` sotto lock**: il descrittore del socket viene copiato sotto
  lock e la scrittura avviene dopo averlo rilasciato, così un client con la
  connessione lenta non tiene bloccati gli altri thread
- **Cambi di stato atomici**: le transizioni di una partita (accettazione,
  inizio, turno, fine, uscita di un giocatore) avvengono sotto il lock della
  partita. Chi esce legge e aggiorna l'avversario nella stessa sezione critica:
  se cadono entrambi nello stesso istante, il secondo trova la partita già
  senza avversario e la rimuove, quindi non restano partite "orfane" che
  occupano uno slot
- **Caduta dell'avversario**: chi resta passa in uno stato di attesa della
  scelta (`w`/`e`) e diventa creatore della partita. La partita resta chiusa
  (non listata, non joinabile, comandi di gioco rifiutati con errore) finché non
  viene presa la decisione; solo con `WAIT_NEW_PLAYER` torna aperta
- **Gestione segnali**: `SIGINT`/`SIGTERM` intercettati per arrestare il server,
  `SIGPIPE` ignorato così una scrittura su un socket già chiuso non termina il
  processo ma restituisce un errore
- **Rilevamento disconnessioni**: una `read()` che ritorna 0 (o un errore) viene
  trattata come disconnessione del client: l'avversario viene avvisato, la
  partita aggiornata e lo slot del giocatore liberato
- **Limiti**: massimo 64 giocatori connessi e 32 partite contemporanee
  (oltre questa soglia il server risponde con l'errore 500 "Server pieno")

## 📝 Author

  Alessio Anepeta : https://github.com/alessioanepetagit/alessioanepetagit
