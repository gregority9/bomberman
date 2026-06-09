#ifndef GAME_H
#define GAME_H

#include <pthread.h>
#include "protocol.h"
#include "queue.h"



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
    int owner;   
    int timer;   
    int range;   
} Bomb;

typedef struct {
    int  connected;
    int  ready;          
    int  alive;
    int  x, y;
    int  score;
    int  active_bombs;   
    int  max_bombs;
    int  range;
    char name[MAX_NAME];
} Player;

typedef struct {
    TileType        board[BOARD_H][BOARD_W];
    int             fire[BOARD_H][BOARD_W];   
    Bomb            bombs[MAX_BOMBS];
    Player          players[MAX_PLAYERS];
    GamePhase       phase;
    long            tick;
    int             roundover_timer;
    int             start_requested;          
    int             last_winner;              
    char            message[128];             
    pthread_mutex_t mutex;                    
} GameState;

void game_init(GameState *g);
void game_destroy(GameState *g);


int  game_add_player(GameState *g, const char *name);   
void game_remove_player(GameState *g, int id);


void game_apply_command(GameState *g, const Command *cmd);


void game_update(GameState *g);


int  game_serialize(GameState *g, char *buf, int buflen);


int  game_count_connected(GameState *g);

#endif 
