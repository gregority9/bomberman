#include "queue.h"

void
queue_init(CommandQueue *q)
{
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    pthread_mutex_init(&q->mutex, NULL);
}

void
queue_destroy(CommandQueue *q)
{
    pthread_mutex_destroy(&q->mutex);
}

int
queue_push(CommandQueue *q, Command cmd)
{
    int ok = 0;

    pthread_mutex_lock(&q->mutex);          /* WEJSCIE do sekcji krytycznej */
    if (q->count < QUEUE_CAP) {
        q->items[q->tail] = cmd;
        q->tail = (q->tail + 1) % QUEUE_CAP;
        q->count++;
        ok = 1;
    }
    pthread_mutex_unlock(&q->mutex);        /* WYJSCIE z sekcji krytycznej  */

    return ok;
}

int
queue_pop(CommandQueue *q, Command *out)
{
    int ok = 0;

    pthread_mutex_lock(&q->mutex);          /* WEJSCIE do sekcji krytycznej */
    if (q->count > 0) {
        *out = q->items[q->head];
        q->head = (q->head + 1) % QUEUE_CAP;
        q->count--;
        ok = 1;
    }
    pthread_mutex_unlock(&q->mutex);        /* WYJSCIE z sekcji krytycznej  */

    return ok;
}
