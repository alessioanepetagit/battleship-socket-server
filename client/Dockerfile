# --- Stage 1: compilazione ---
FROM gcc:13 AS builder
WORKDIR /app
COPY src/ ./src/
RUN gcc -Wall -Wextra src/main.c src/client.c src/ui.c -o client -I src -pthread

# --- Stage 2: immagine finale ---
FROM debian:bookworm-slim
WORKDIR /app
# locale UTF-8: senza questo le cornici della griglia escono a pezzi
ENV LANG=C.UTF-8
COPY --from=builder /app/client ./client
# host e porta arrivano dal docker-compose come variabili d'ambiente
ENV SERVER_HOST=server
ENV SERVER_PORT=8080
CMD ["./client"]
