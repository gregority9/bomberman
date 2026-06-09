#ifndef SERVER_H
#define SERVER_H

#include <pthread.h>
#include "protocol.h"
#include "game.h"
#include "queue.h"

/*
 * server.h
 * --------------------------------------------------------------------------
 * Warstwa sieciowa: gniazdo nasluchujace TCP, watki obslugi klientow oraz
 * rozglaszanie (broadcast) stanu gry.
 *
 * Kazdy klient obslugiwany jest przez wlasny watek POSIX, ktory odbiera
 * komendy i wklada je do wspoldzielonej kolejki polecen (sekcja krytyczna).
 * Tablica gniazd klientow jest osobnym wspoldzielonym zasobem chronionym
 * wlasnym muteksem (clients_mutex).
 * --------------------------------------------------------------------------
 */

typedef struct {
    GameState      *game;
    CommandQueue   *queue;
    int             listen_fd;
    int             port;
    volatile int    running;

    int             client_fd[MAX_PLAYERS]; /* gniazdo wg id gracza, -1 = wolne */
    pthread_mutex_t clients_mutex;
} Server;

/* Argument przekazywany do watku klienta. */
typedef struct {
    Server *srv;
    int     fd;
} ClientArg;

int  server_init(Server *srv, GameState *game, CommandQueue *queue, int port);
void server_start_accept_loop(Server *srv);   /* tworzy watek accept */
void server_broadcast(Server *srv, const char *msg, int len);
void server_shutdown(Server *srv);

#endif /* SERVER_H */
