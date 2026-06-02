#ifndef GAME_H
#define GAME_H

#include <pthread.h>
#include "protocol.h"
#include "queue.h"

/*
 * game.h
 * --------------------------------------------------------------------------
 * Autorytatywny stan gry oraz logika rozgrywki.
 *
 * SEKCJA KRYTYCZNA: cala struktura GameState (plansza, bomby, gracze, liczniki
 * aktywnych bomb) jest wspoldzielona miedzy watkiem glownej petli a watkami
 * klientow (np. przy dolaczaniu / rozlaczaniu gracza). Dostep nalezy
 * serializowac przez GameState.mutex.
 *
 * Konwencja blokowania:
 *   - game_add_player / game_remove_player / game_serialize / game_count_connected
 *     blokuja mutex SAMODZIELNIE (wywolywane z watkow klientow).
 *   - game_apply_command / game_update zakladaja, ze mutex jest JUZ zablokowany
 *     przez wywolujacego (glowna petla blokuje raz na caly tick).
 * --------------------------------------------------------------------------
 */

typedef enum {
    TILE_EMPTY = 0,
    TILE_WALL  = 1,
    TILE_BOX   = 2
} TileType;

typedef enum {
    DIR_UP = 0,
    DIR_DOWN,
    DIR_LEFT,
    DIR_RIGHT
} Direction;

typedef enum {
    PHASE_WAITING   = 0,
    PHASE_PLAYING   = 1,
    PHASE_ROUNDOVER = 2
} GamePhase;

typedef struct {
    int active;
    int x, y;
    int owner;   /* id gracza, ktory postawil bombe */
    int timer;   /* ticki pozostale do wybuchu        */
    int range;   /* zasieg fali uderzeniowej           */
} Bomb;

typedef struct {
    int  connected;
    int  ready;          /* gotowosc w lobby (faza WAITING) */
    int  alive;
    int  x, y;
    int  score;
    int  active_bombs;   /* LICZNIK aktywnych bomb - sekcja krytyczna */
    int  max_bombs;
    int  range;
    char name[MAX_NAME];
} Player;

typedef struct {
    TileType        board[BOARD_H][BOARD_W];
    int             fire[BOARD_H][BOARD_W];   /* ticki pozostale ognia (0 = brak) */
    Bomb            bombs[MAX_BOMBS];
    Player          players[MAX_PLAYERS];
    GamePhase       phase;
    long            tick;
    int             roundover_timer;
    int             start_requested;          /* ustawiane przez komende START */
    int             last_winner;              /* id zwyciezcy, -1 = remis/brak */
    char            message[128];             /* ostatni komunikat informacyjny */
    pthread_mutex_t mutex;                    /* chroni CALY stan gry           */
} GameState;

void game_init(GameState *g);
void game_destroy(GameState *g);

/* Rejestracja / wyrejestrowanie gracza (wlasne blokowanie muteksu). */
int  game_add_player(GameState *g, const char *name);   /* zwraca id lub -1 */
void game_remove_player(GameState *g, int id);

/* Stosuje jedno polecenie z kolejki. Wymaga zablokowanego muteksu. */
void game_apply_command(GameState *g, const Command *cmd);

/* Aktualizacja logiki tick: bomby, ogien, eliminacje, rundy. Wymaga blokady. */
void game_update(GameState *g);

/* Serializacja stanu do bufora tekstowego (wlasne blokowanie). */
int  game_serialize(GameState *g, char *buf, int buflen);

/* Liczba podlaczonych graczy (wlasne blokowanie). */
int  game_count_connected(GameState *g);

#endif /* GAME_H */
