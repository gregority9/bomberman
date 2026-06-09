#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>



#define QUEUE_CAP 1024

typedef enum {
    CMD_MOVE,
    CMD_BOMB,
    CMD_READY,   
    CMD_START    
} CommandType;

typedef struct {
    int         player_id;
    CommandType type;
    int         dir;        
} Command;

typedef struct {
    Command         items[QUEUE_CAP];
    int             head;   
    int             tail;   
    int             count;  
    pthread_mutex_t mutex;  
} CommandQueue;

void queue_init(CommandQueue *q);
void queue_destroy(CommandQueue *q);


int  queue_push(CommandQueue *q, Command cmd);


int  queue_pop(CommandQueue *q, Command *out);

#endif 
