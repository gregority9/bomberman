#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* --------------------------------------------------------------------------
 * Rejestr gniazd klientow (sekcja krytyczna: clients_mutex)
 * -------------------------------------------------------------------------- */

static void
register_client(Server *srv, int id, int fd)
{
    pthread_mutex_lock(&srv->clients_mutex);
    srv->client_fd[id] = fd;
    pthread_mutex_unlock(&srv->clients_mutex);
}

static void
unregister_client(Server *srv, int id)
{
    pthread_mutex_lock(&srv->clients_mutex);
    srv->client_fd[id] = -1;
    pthread_mutex_unlock(&srv->clients_mutex);
}

void
server_broadcast(Server *srv, const char *msg, int len)
{
    int i;

    pthread_mutex_lock(&srv->clients_mutex);
    for (i = 0; i < MAX_PLAYERS; i++) {
        if (srv->client_fd[i] >= 0) {
            /* MSG_NOSIGNAL: nie zabijaj procesu gdy klient zerwal polaczenie. */
            if (send(srv->client_fd[i], msg, len, MSG_NOSIGNAL) < 0) {
                /* Watek klienta wykryje rozlaczenie przy recv i posprzata. */
            }
        }
    }
    pthread_mutex_unlock(&srv->clients_mutex);
}

/* Wysyla pojedyncza wiadomosc do jednego gniazda. */
static void
send_line(int fd, const char *msg)
{
    send(fd, msg, strlen(msg), MSG_NOSIGNAL);
}

/* --------------------------------------------------------------------------
 * Parsowanie komend od klienta
 * -------------------------------------------------------------------------- */

static int
parse_direction(const char *s, Direction *out)
{
    if      (strncmp(s, "UP", 2) == 0)    { *out = DIR_UP;    return 1; }
    else if (strncmp(s, "DOWN", 4) == 0)  { *out = DIR_DOWN;  return 1; }
    else if (strncmp(s, "LEFT", 4) == 0)  { *out = DIR_LEFT;  return 1; }
    else if (strncmp(s, "RIGHT", 5) == 0) { *out = DIR_RIGHT; return 1; }
    return 0;
}

/* Obsluga jednej linii polecenia. Zwraca 0 gdy klient ma sie rozlaczyc. */
static int
handle_line(Server *srv, int player_id, char *line)
{
    Direction dir;

    /* Usun konce linii. */
    line[strcspn(line, "\r\n")] = '\0';

    if (strncmp(line, "MOVE ", 5) == 0) {
        if (parse_direction(line + 5, &dir)) {
            Command c;
            c.player_id = player_id;
            c.type      = CMD_MOVE;
            c.dir       = (int)dir;
            queue_push(srv->queue, c);    /* do wspoldzielonej kolejki polecen */
        }
    }
    else if (strncmp(line, "BOMB", 4) == 0) {
        Command c;
        c.player_id = player_id;
        c.type      = CMD_BOMB;
        c.dir       = 0;
        queue_push(srv->queue, c);
    }
    else if (strncmp(line, "READY", 5) == 0) {
        Command c;
        c.player_id = player_id;
        c.type      = CMD_READY;
        c.dir       = 0;
        queue_push(srv->queue, c);
    }
    else if (strncmp(line, "START", 5) == 0) {
        Command c;
        c.player_id = player_id;
        c.type      = CMD_START;
        c.dir       = 0;
        queue_push(srv->queue, c);
    }
    else if (strncmp(line, "QUIT", 4) == 0) {
        return 0;
    }

    return 1;
}

/* --------------------------------------------------------------------------
 * Watek pojedynczego klienta
 * -------------------------------------------------------------------------- */

static void *
client_thread(void *arg)
{
    ClientArg *ca = (ClientArg *)arg;
    Server    *srv = ca->srv;
    int        fd  = ca->fd;
    int        player_id = -1;
    char       buf[1024];
    char       line[1024];
    int        line_len = 0;
    char       welcome[64];
    int        n, i;

    free(ca);
    pthread_detach(pthread_self());

    /* --- Faza dolaczenia: oczekiwanie na "JOIN <nazwa>" --- */
    for (;;) {
        n = recv(fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) { close(fd); return NULL; }
        buf[n] = '\0';

        /* Zbieraj do napotkania znaku konca linii. */
        for (i = 0; i < n; i++) {
            if (buf[i] == '\n' || buf[i] == '\r') {
                line[line_len] = '\0';
                goto got_join_line;
            } else if (line_len < (int)sizeof(line) - 1) {
                line[line_len++] = buf[i];
            }
        }
        continue;

    got_join_line:
        if (strncmp(line, "JOIN ", 5) == 0) {
            const char *name = line + 5;
            player_id = game_add_player(srv->game, name);
            if (player_id < 0) {
                send_line(fd, "MSG|Serwer pelny\n");
                close(fd);
                return NULL;
            }
            register_client(srv, player_id, fd);
            snprintf(welcome, sizeof(welcome), "WELCOME|%d|%d|%d\n",
                     player_id, BOARD_W, BOARD_H);
            send_line(fd, welcome);
            break;
        }
        /* Niepoprawna pierwsza linia - czekaj na kolejna. */
        line_len = 0;
    }

    fprintf(stderr, "[serwer] gracz %d dolaczyl\n", player_id);

    /* --- Petla odbioru komend (z buforowaniem niepelnych linii) --- */
    line_len = 0;
    for (;;) {
        n = recv(fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0)
            break;                       /* rozlaczenie lub blad */
        buf[n] = '\0';

        for (i = 0; i < n; i++) {
            if (buf[i] == '\n' || buf[i] == '\r') {
                if (line_len > 0) {
                    line[line_len] = '\0';
                    if (!handle_line(srv, player_id, line))
                        goto disconnect;
                    line_len = 0;
                }
            } else if (line_len < (int)sizeof(line) - 1) {
                line[line_len++] = buf[i];
            }
        }
    }

disconnect:
    fprintf(stderr, "[serwer] gracz %d rozlaczyl sie\n", player_id);
    unregister_client(srv, player_id);
    game_remove_player(srv->game, player_id);
    close(fd);
    return NULL;
}

/* --------------------------------------------------------------------------
 * Watek akceptujacy nowe polaczenia
 * -------------------------------------------------------------------------- */

static void *
accept_thread(void *arg)
{
    Server *srv = (Server *)arg;

    while (srv->running) {
        struct sockaddr_in cli;
        socklen_t          clilen = sizeof(cli);
        int                fd;
        pthread_t          tid;
        ClientArg         *ca;

        fd = accept(srv->listen_fd, (struct sockaddr *)&cli, &clilen);
        if (fd < 0) {
            if (srv->running)
                perror("accept");
            continue;
        }

        fprintf(stderr, "[serwer] nowe polaczenie z %s\n",
                inet_ntoa(cli.sin_addr));

        ca = (ClientArg *)malloc(sizeof(ClientArg));
        ca->srv = srv;
        ca->fd  = fd;
        if (pthread_create(&tid, NULL, client_thread, ca) != 0) {
            perror("pthread_create");
            close(fd);
            free(ca);
        }
    }
    return NULL;
}

/* --------------------------------------------------------------------------
 * Inicjalizacja / uruchomienie / zamkniecie
 * -------------------------------------------------------------------------- */

int
server_init(Server *srv, GameState *game, CommandQueue *queue, int port)
{
    struct sockaddr_in addr;
    int opt = 1;
    int i;

    srv->game    = game;
    srv->queue   = queue;
    srv->port    = port;
    srv->running = 1;
    pthread_mutex_init(&srv->clients_mutex, NULL);
    for (i = 0; i < MAX_PLAYERS; i++)
        srv->client_fd[i] = -1;

    srv->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv->listen_fd < 0) {
        perror("socket");
        return -1;
    }

    setsockopt(srv->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons((unsigned short)port);

    if (bind(srv->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(srv->listen_fd);
        return -1;
    }

    if (listen(srv->listen_fd, 16) < 0) {
        perror("listen");
        close(srv->listen_fd);
        return -1;
    }

    fprintf(stderr, "[serwer] nasluchuje na porcie %d\n", port);
    return 0;
}

void
server_start_accept_loop(Server *srv)
{
    pthread_t tid;
    pthread_create(&tid, NULL, accept_thread, srv);
    pthread_detach(tid);
}

void
server_shutdown(Server *srv)
{
    srv->running = 0;
    close(srv->listen_fd);
    pthread_mutex_destroy(&srv->clients_mutex);
}
