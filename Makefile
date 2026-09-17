# Compilazione locale (senza Docker)
CC      = gcc
CFLAGS  = -Wall -Wextra -pthread
SRV_SRC = $(wildcard server/src/*.c)
CLI_SRC = $(wildcard client/src/*.c)

all: server/server client/client

server/server: $(SRV_SRC)
	$(CC) $(CFLAGS) -I server/src $(SRV_SRC) -o $@

client/client: $(CLI_SRC)
	$(CC) $(CFLAGS) -I client/src $(CLI_SRC) -o $@

run-server: server/server
	./server/server -p 8080

run-client: client/client
	./client/client -h 127.0.0.1 -p 8080

clean:
	rm -f server/server client/client

docker-up:
	docker compose up -d --build server

docker-down:
	docker compose down

.PHONY: all clean run-server run-client docker-up docker-down
