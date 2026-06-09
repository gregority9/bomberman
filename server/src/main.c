#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#include "protocol.h"
#include "game.h"
#include "queue.h"
#include "server.h"

static volatile int g_running = 1;

static void
on_sigint(int sig)
{
    (void)sig;
    g_running = 0;
}

/* Usypia watek na okreslona liczbe milisekund. */
static void
sleep_ms(long ms)
{
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

int
main(int argc, char *argv[])
{
    int          port = DEFAULT_PORT;
    GameState    game;
    CommandQueue queue;
    Server       srv;
    char         state_buf[STATE_BUF_SIZE];
    GamePhase    last_phase;
    char         last_message[128];

    if (argc > 1)
        port = atoi(argv[1]);

    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);
#ifdef SIGPIPE
    signal(SIGPIPE, SIG_IGN);   /* ignoruj zerwane gniazda przy send() */
#endif

    /* Inicjalizacja wspoldzielonych zasobow. */
    game_init(&game);
    queue_init(&queue);

    if (server_init(&srv, &game, &queue, port) != 0) {
        fprintf(stderr, "Nie udalo sie uruchomic serwera.\n");
        return 1;
    }

    server_start_accept_loop(&srv);

    last_phase = game.phase;
    last_message[0] = '\0';

    fprintf(stderr, "[serwer] glowna petla gry: %d tickow/s\n", 1000 / TICK_MS);

    /* ---------------- GLOWNA PETLA GRY (tick-rate) ---------------- */
    while (g_running) {
        Command cmd;
        int     len;

        /* SEKCJA KRYTYCZNA: caly tick liczony pod jednym muteksem stanu gry. */
        pthread_mutex_lock(&game.mutex);

        /* 1. Pobierz i zastosuj wszystkie zalegle polecenia z kolejki. */
        while (queue_pop(&queue, &cmd))
            game_apply_command(&game, &cmd);

        /* 2. Przelicz logike: bomby, ogien, eliminacje, rundy. */
        game_update(&game);

        pthread_mutex_unlock(&game.mutex);

        /* 3. Serializacja i rozglaszanie stanu do wszystkich klientow. */
        len = game_serialize(&game, state_buf, sizeof(state_buf));
        if (len > 0)
            server_broadcast(&srv, state_buf, len);

        /* 4. Powiadomienia o zmianie fazy / komunikatach (np. zwyciezca). */
        pthread_mutex_lock(&game.mutex);
        if (game.phase != last_phase ||
            strcmp(game.message, last_message) != 0) {
            char msg[160];
            snprintf(msg, sizeof(msg), "MSG|%s\n", game.message);
            last_phase = game.phase;
            snprintf(last_message, sizeof(last_message), "%s", game.message);
            pthread_mutex_unlock(&game.mutex);
            server_broadcast(&srv, msg, strlen(msg));
        } else {
            pthread_mutex_unlock(&game.mutex);
        }

        sleep_ms(TICK_MS);
    }

    fprintf(stderr, "\n[serwer] zamykanie...\n");
    server_shutdown(&srv);
    queue_destroy(&queue);
    game_destroy(&game);
    return 0;
}
