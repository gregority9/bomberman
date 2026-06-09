#ifndef SERVER_H
#define SERVER_H

#include <pthread.h>
#include "protocol.h"
#include "game.h"
#include "queue.h"



typedef struct {
    GameState      *game;
    CommandQueue   *queue;
    int             listen_fd;
    int             port;
    volatile int    running;

    int             client_fd[MAX_PLAYERS]; 
    pthread_mutex_t clients_mutex;
} Server;


typedef struct {
    Server *srv;
    int     fd;
} ClientArg;

int  server_init(Server *srv, GameState *game, CommandQueue *queue, int port);
void server_start_accept_loop(Server *srv);   
void server_broadcast(Server *srv, const char *msg, int len);
void server_shutdown(Server *srv);

#endif 
