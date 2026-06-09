#include "game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Pozycje startowe graczy: cztery narozniki planszy. */
static const int spawn_x[MAX_PLAYERS] = { 1, BOARD_W - 2, 1,            BOARD_W - 2 };
static const int spawn_y[MAX_PLAYERS] = { 1, 1,           BOARD_H - 2,  BOARD_H - 2 };

/* --------------------------------------------------------------------------
 * Pomocnicze
 * -------------------------------------------------------------------------- */

static int
in_bounds(int x, int y)
{
    return x >= 0 && x < BOARD_W && y >= 0 && y < BOARD_H;
}

/* Czy w okolicy naroznika (zeby gracz mial pole manewru na starcie). */
static int
is_spawn_safe_zone(int x, int y)
{
    int i;
    for (i = 0; i < MAX_PLAYERS; i++) {
        int dx = x - spawn_x[i];
        int dy = y - spawn_y[i];
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        /* Samo pole startowe oraz pola bezposrednio przylegle. */
        if ((dx == 0 && dy <= 1) || (dy == 0 && dx <= 1))
            return 1;
    }
    return 0;
}

/* Znajduje indeks aktywnej bomby na danym polu lub -1. */
static int
find_bomb_at(GameState *g, int x, int y)
{
    int i;
    for (i = 0; i < MAX_BOMBS; i++) {
        if (g->bombs[i].active && g->bombs[i].x == x && g->bombs[i].y == y)
            return i;
    }
    return -1;
}

/* Czy na polu stoi zywy gracz (inny niz except_id). */
static int
player_at(GameState *g, int x, int y, int except_id)
{
    int i;
    for (i = 0; i < MAX_PLAYERS; i++) {
        if (i == except_id) continue;
        if (g->players[i].connected && g->players[i].alive &&
            g->players[i].x == x && g->players[i].y == y)
            return 1;
    }
    return 0;
}

/* --------------------------------------------------------------------------
 * Generowanie planszy i restart rundy
 * -------------------------------------------------------------------------- */

static void
generate_board(GameState *g)
{
    int x, y;

    for (y = 0; y < BOARD_H; y++) {
        for (x = 0; x < BOARD_W; x++) {
            g->fire[y][x] = 0;

            /* Obramowanie planszy = sciany niezniszczalne. */
            if (x == 0 || y == 0 || x == BOARD_W - 1 || y == BOARD_H - 1) {
                g->board[y][x] = TILE_WALL;
            }
            /* Filary co drugie pole (klasyczny uklad Bomberman). */
            else if (x % 2 == 0 && y % 2 == 0) {
                g->board[y][x] = TILE_WALL;
            }
            /* Pozostale pola: losowo skrzynki, ale nie w strefach startowych. */
            else if (!is_spawn_safe_zone(x, y) && (rand() % 100) < 75) {
                g->board[y][x] = TILE_BOX;
            }
            else {
                g->board[y][x] = TILE_EMPTY;
            }
        }
    }
}

void
game_reset_round(GameState *g)
{
    int i;

    generate_board(g);

    for (i = 0; i < MAX_BOMBS; i++)
        g->bombs[i].active = 0;

    for (i = 0; i < MAX_PLAYERS; i++) {
        Player *p = &g->players[i];
        if (!p->connected)
            continue;
        p->alive        = 1;
        p->x            = spawn_x[i];
        p->y            = spawn_y[i];
        p->active_bombs = 0;
        p->max_bombs    = DEFAULT_MAX_BOMBS;
        p->range        = DEFAULT_RANGE;
    }

    g->phase           = PHASE_PLAYING;
    g->roundover_timer = 0;
    g->start_requested = 0;
    g->last_winner     = -1;
    g->message[0]      = '\0';
}

void
game_init(GameState *g)
{
    int i;

    srand((unsigned int)time(NULL));
    memset(g, 0, sizeof(*g));

    for (i = 0; i < MAX_PLAYERS; i++) {
        g->players[i].connected = 0;
        g->players[i].alive     = 0;
        g->players[i].score     = 0;
    }
    g->phase           = PHASE_WAITING;
    g->tick            = 0;
    g->last_winner     = -1;
    g->start_requested = 0;
    strncpy(g->message, "Lobby: oczekiwanie na graczy...",
            sizeof(g->message) - 1);

    generate_board(g);
    pthread_mutex_init(&g->mutex, NULL);
}

void
game_destroy(GameState *g)
{
    pthread_mutex_destroy(&g->mutex);
}

/* --------------------------------------------------------------------------
 * Zarzadzanie graczami (wlasne blokowanie muteksu)
 * -------------------------------------------------------------------------- */

int
game_add_player(GameState *g, const char *name)
{
    int id = -1;
    int i;

    pthread_mutex_lock(&g->mutex);
    for (i = 0; i < MAX_PLAYERS; i++) {
        if (!g->players[i].connected) {
            Player *p = &g->players[i];
            p->connected    = 1;
            p->ready        = 0;       /* w lobby trzeba oznaczyc gotowosc */
            p->alive        = 0;       /* ozyje przy starcie rundy */
            p->x            = spawn_x[i];
            p->y            = spawn_y[i];
            p->active_bombs = 0;
            p->max_bombs    = DEFAULT_MAX_BOMBS;
            p->range        = DEFAULT_RANGE;
            strncpy(p->name, name, MAX_NAME - 1);
            p->name[MAX_NAME - 1] = '\0';
            id = i;
            break;
        }
    }
    pthread_mutex_unlock(&g->mutex);

    return id;
}

void
game_remove_player(GameState *g, int id)
{
    if (id < 0 || id >= MAX_PLAYERS)
        return;

    pthread_mutex_lock(&g->mutex);
    g->players[id].connected = 0;
    g->players[id].alive     = 0;
    pthread_mutex_unlock(&g->mutex);
}

int
game_count_connected(GameState *g)
{
    int n = 0;
    int i;

    pthread_mutex_lock(&g->mutex);
    for (i = 0; i < MAX_PLAYERS; i++)
        if (g->players[i].connected)
            n++;
    pthread_mutex_unlock(&g->mutex);

    return n;
}

/* --------------------------------------------------------------------------
 * Stosowanie polecen (mutex trzymany przez wywolujacego)
 * -------------------------------------------------------------------------- */

static void
apply_move(GameState *g, int id, Direction dir)
{
    Player *p = &g->players[id];
    int nx, ny;

    if (!p->connected || !p->alive)
        return;

    nx = p->x;
    ny = p->y;
    switch (dir) {
        case DIR_UP:    ny--; break;
        case DIR_DOWN:  ny++; break;
        case DIR_LEFT:  nx--; break;
        case DIR_RIGHT: nx++; break;
        default: return;
    }

    if (!in_bounds(nx, ny))
        return;
    if (g->board[ny][nx] != TILE_EMPTY)        /* sciana lub skrzynka blokuja */
        return;
    if (find_bomb_at(g, nx, ny) != -1)         /* aktywna bomba blokuje       */
        return;
    if (player_at(g, nx, ny, id))              /* pole zajete przez gracza    */
        return;

    p->x = nx;
    p->y = ny;
}

static void
place_bomb(GameState *g, int id)
{
    Player *p = &g->players[id];
    int i;

    if (!p->connected || !p->alive)
        return;
    if (p->active_bombs >= p->max_bombs)       /* limit bomb - sekcja krytyczna */
        return;
    if (find_bomb_at(g, p->x, p->y) != -1)     /* juz jest tu bomba             */
        return;

    for (i = 0; i < MAX_BOMBS; i++) {
        if (!g->bombs[i].active) {
            g->bombs[i].active = 1;
            g->bombs[i].x      = p->x;
            g->bombs[i].y      = p->y;
            g->bombs[i].owner  = id;
            g->bombs[i].timer  = BOMB_TIMER_TICKS;
            g->bombs[i].range  = p->range;
            p->active_bombs++;
            return;
        }
    }
}

void
game_apply_command(GameState *g, const Command *cmd)
{
    int id = cmd->player_id;

    if (id < 0 || id >= MAX_PLAYERS || !g->players[id].connected)
        return;

    switch (cmd->type) {
        /* Komendy lobby - dzialaja tylko w fazie oczekiwania. */
        case CMD_READY:
            if (g->phase == PHASE_WAITING)
                g->players[id].ready = !g->players[id].ready;
            break;
        case CMD_START:
            if (g->phase == PHASE_WAITING)
                g->start_requested = 1;
            break;
        /* Komendy rozgrywki - tylko w trakcie gry. */
        case CMD_MOVE:
            if (g->phase == PHASE_PLAYING)
                apply_move(g, id, (Direction)cmd->dir);
            break;
        case CMD_BOMB:
            if (g->phase == PHASE_PLAYING)
                place_bomb(g, id);
            break;
    }
}

/* --------------------------------------------------------------------------
 * Wybuchy
 * -------------------------------------------------------------------------- */

/* Rozchodzi ogien z bomby b w czterech kierunkach. Bomby trafione fala sa
 * dopisywane do listy "to_explode" (lancuchowe wybuchy). */
static void
detonate(GameState *g, int bomb_idx, int *to_explode, int *to_explode_n)
{
    static const int dx[4] = {  0,  0, -1, 1 };
    static const int dy[4] = { -1,  1,  0, 0 };
    Bomb *b = &g->bombs[bomb_idx];
    int range = b->range;
    int d, r;

    if (!b->active)
        return;

    b->active = 0;
    if (g->players[b->owner].connected && g->players[b->owner].active_bombs > 0)
        g->players[b->owner].active_bombs--;   /* zwolnienie licznika bomb */

    /* Srodek wybuchu. */
    g->fire[b->y][b->x] = FIRE_DURATION_TICKS;

    for (d = 0; d < 4; d++) {
        for (r = 1; r <= range; r++) {
            int nx = b->x + dx[d] * r;
            int ny = b->y + dy[d] * r;
            int other;

            if (!in_bounds(nx, ny))
                break;
            if (g->board[ny][nx] == TILE_WALL)         /* sciana zatrzymuje fale */
                break;

            g->fire[ny][nx] = FIRE_DURATION_TICKS;

            if (g->board[ny][nx] == TILE_BOX) {         /* skrzynka: niszczona i stop */
                g->board[ny][nx] = TILE_EMPTY;
                break;
            }

            other = find_bomb_at(g, nx, ny);            /* lancuchowy wybuch */
            if (other != -1 && *to_explode_n < MAX_BOMBS) {
                to_explode[(*to_explode_n)++] = other;
            }
        }
    }
}

static void
process_bombs(GameState *g)
{
    int to_explode[MAX_BOMBS];
    int n = 0;
    int i;

    /* Odliczanie i kwalifikacja bomb do wybuchu w tym ticku. */
    for (i = 0; i < MAX_BOMBS; i++) {
        if (g->bombs[i].active) {
            g->bombs[i].timer--;
            if (g->bombs[i].timer <= 0)
                to_explode[n++] = i;
        }
    }

    /* Przetwarzanie listy wybuchow z obsluga lancuchow (detonate dopisuje). */
    for (i = 0; i < n; i++)
        detonate(g, to_explode[i], to_explode, &n);
}

static void
age_fire(GameState *g)
{
    int x, y;
    for (y = 0; y < BOARD_H; y++)
        for (x = 0; x < BOARD_W; x++)
            if (g->fire[y][x] > 0)
                g->fire[y][x]--;
}

static void
kill_players_in_fire(GameState *g)
{
    int i;
    for (i = 0; i < MAX_PLAYERS; i++) {
        Player *p = &g->players[i];
        if (p->connected && p->alive && g->fire[p->y][p->x] > 0)
            p->alive = 0;
    }
}

/* --------------------------------------------------------------------------
 * Logika rund
 * -------------------------------------------------------------------------- */

static void
check_round_end(GameState *g)
{
    int alive = 0, connected = 0, last_alive = -1;
    int i;

    for (i = 0; i < MAX_PLAYERS; i++) {
        if (g->players[i].connected) {
            connected++;
            if (g->players[i].alive) {
                alive++;
                last_alive = i;
            }
        }
    }

    /* Runda konczy sie gdy zostanie <= 1 zywy gracz (przy >=2 uczestnikach). */
    if (alive <= 1) {
        g->phase           = PHASE_ROUNDOVER;
        g->roundover_timer = ROUNDOVER_TICKS;

        if (alive == 1) {
            g->last_winner = last_alive;
            g->players[last_alive].score++;
            snprintf(g->message, sizeof(g->message),
                     "Runde wygral gracz %d (%s)!",
                     last_alive, g->players[last_alive].name);
        } else {
            g->last_winner = -1;             /* remis - nikt nie dostaje punktu */
            snprintf(g->message, sizeof(g->message), "Remis! Nikt nie przetrwal.");
        }
    }
}

void
game_update(GameState *g)
{
    int connected;
    int i;

    g->tick++;

    if (g->phase == PHASE_WAITING) {
        int ready = 0;
        connected = 0;
        for (i = 0; i < MAX_PLAYERS; i++) {
            if (g->players[i].connected) {
                connected++;
                if (g->players[i].ready)
                    ready++;
            }
        }

        /* Start tylko na wyrazne zadanie (START) i gdy WSZYSCY sa gotowi. */
        if (g->start_requested && connected >= MIN_PLAYERS && ready == connected) {
            game_reset_round(g);
        } else if (connected < MIN_PLAYERS) {
            snprintf(g->message, sizeof(g->message),
                     "Lobby: oczekiwanie na graczy (%d/%d)...",
                     connected, MIN_PLAYERS);
        } else {
            snprintf(g->message, sizeof(g->message),
                     "Lobby: gotowych %d/%d - oznacz gotowosc i nacisnij START",
                     ready, connected);
        }
        g->start_requested = 0;     /* zadanie obowiazuje tylko w tym ticku */
        return;
    }

    if (g->phase == PHASE_ROUNDOVER) {
        if (g->roundover_timer > 0)
            g->roundover_timer--;
        if (g->roundover_timer <= 0) {
            /* Powrot do lobby: kazdy musi ponownie oznaczyc gotowosc. */
            for (i = 0; i < MAX_PLAYERS; i++)
                g->players[i].ready = 0;
            g->start_requested = 0;
            g->phase = PHASE_WAITING;
            strncpy(g->message, "Lobby: oznacz gotowosc i nacisnij START",
                    sizeof(g->message) - 1);
        }
        return;
    }

    /* PHASE_PLAYING: ruchy graczy zostaly juz zaaplikowane z kolejki polecen. */
    age_fire(g);                 /* starzenie istniejacego ognia            */
    process_bombs(g);            /* odliczanie i wybuchy (tworza nowy ogien) */
    kill_players_in_fire(g);     /* eliminacja graczy stojacych w ogniu      */
    check_round_end(g);          /* warunek konca rundy + punktacja          */
}

/* --------------------------------------------------------------------------
 * Serializacja stanu (wlasne blokowanie muteksu)
 * -------------------------------------------------------------------------- */

int
game_serialize(GameState *g, char *buf, int buflen)
{
    char board[BOARD_W * BOARD_H + 1];
    char bombs[512];
    char fires[1024];
    char players[512];
    int x, y, i, len, pos;

    pthread_mutex_lock(&g->mutex);

    /* Warstwa terenu. */
    pos = 0;
    for (y = 0; y < BOARD_H; y++) {
        for (x = 0; x < BOARD_W; x++) {
            char c = CH_EMPTY;
            if (g->board[y][x] == TILE_WALL) c = CH_WALL;
            else if (g->board[y][x] == TILE_BOX) c = CH_BOX;
            board[pos++] = c;
        }
    }
    board[pos] = '\0';

    /* Bomby. */
    bombs[0] = '\0';
    pos = 0;
    for (i = 0; i < MAX_BOMBS; i++) {
        if (g->bombs[i].active) {
            pos += snprintf(bombs + pos, sizeof(bombs) - pos,
                            "%s%d,%d,%d",
                            pos ? " " : "",
                            g->bombs[i].x, g->bombs[i].y, g->bombs[i].timer);
            if (pos >= (int)sizeof(bombs) - 1) break;
        }
    }

    /* Ogien. */
    fires[0] = '\0';
    pos = 0;
    for (y = 0; y < BOARD_H; y++) {
        for (x = 0; x < BOARD_W; x++) {
            if (g->fire[y][x] > 0) {
                pos += snprintf(fires + pos, sizeof(fires) - pos,
                                "%s%d,%d", pos ? " " : "", x, y);
                if (pos >= (int)sizeof(fires) - 1) break;
            }
        }
        if (pos >= (int)sizeof(fires) - 1) break;
    }

    /* Gracze. */
    players[0] = '\0';
    pos = 0;
    for (i = 0; i < MAX_PLAYERS; i++) {
        if (g->players[i].connected) {
            pos += snprintf(players + pos, sizeof(players) - pos,
                            "%s%d,%d,%d,%d,%d,%d,%s",
                            pos ? " " : "",
                            i, g->players[i].x, g->players[i].y,
                            g->players[i].alive, g->players[i].score,
                            g->players[i].ready, g->players[i].name);
            if (pos >= (int)sizeof(players) - 1) break;
        }
    }

    len = snprintf(buf, buflen,
                   "STATE|%ld|%d|%d|%d|%s|%s|%s|%s\n",
                   g->tick, (int)g->phase, BOARD_W, BOARD_H,
                   board, bombs, fires, players);

    pthread_mutex_unlock(&g->mutex);

    return len;
}
