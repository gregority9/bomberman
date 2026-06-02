#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>

/*
 * queue.h
 * --------------------------------------------------------------------------
 * Bezpieczna watkowo (thread-safe) kolejka polecen FIFO.
 *
 * Jest to KLUCZOWA sekcja krytyczna projektu: watki nasluchujace klientow
 * (producenci) wkladaja tu odebrane komendy, a glowny watek gry (konsument)
 * pobiera je raz na tick. Dostep jest serializowany muteksem POSIX, dzieki
 * czemu nie dochodzi do wyscigow (race conditions) na wspoldzielonym buforze.
 * --------------------------------------------------------------------------
 */

#define QUEUE_CAP 1024

typedef enum {
    CMD_MOVE,
    CMD_BOMB,
    CMD_READY,   /* przelaczenie gotowosci gracza w lobby */
    CMD_START    /* zadanie rozpoczecia rozgrywki          */
} CommandType;

typedef struct {
    int         player_id;
    CommandType type;
    int         dir;        /* znaczace tylko dla CMD_MOVE (Direction) */
} Command;

typedef struct {
    Command         items[QUEUE_CAP];
    int             head;   /* indeks pierwszego elementu do pobrania */
    int             tail;   /* indeks nastepnego wolnego miejsca       */
    int             count;  /* liczba elementow w kolejce               */
    pthread_mutex_t mutex;  /* zabezpiecza cala strukture               */
} CommandQueue;

void queue_init(CommandQueue *q);
void queue_destroy(CommandQueue *q);

/* Dodaje polecenie. Zwraca 1 przy sukcesie, 0 gdy kolejka pelna. */
int  queue_push(CommandQueue *q, Command cmd);

/* Pobiera polecenie do *out. Zwraca 1 gdy cos pobrano, 0 gdy kolejka pusta. */
int  queue_pop(CommandQueue *q, Command *out);

#endif /* QUEUE_H */
