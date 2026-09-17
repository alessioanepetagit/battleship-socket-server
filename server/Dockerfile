# --- Stage 1: compilazione ---
FROM gcc:13 AS builder
WORKDIR /app
COPY src/ ./src/
RUN gcc -Wall -Wextra src/main.c src/server.c src/game_manager.c src/game_logic.c \
    src/protocol.c src/client_handler.c -o server -I src -pthread

# --- Stage 2: immagine finale ---
FROM debian:bookworm-slim
WORKDIR /app
COPY --from=builder /app/server ./server
EXPOSE 8080
CMD ["./server", "-p", "8080"]
