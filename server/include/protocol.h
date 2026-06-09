#ifndef PROTOCOL_H
#define PROTOCOL_H

/*
 * protocol.h
 * --------------------------------------------------------------------------
 * Wspolne stale oraz definicja prostego, tekstowego protokolu komunikacyjnego
 * gry "Bombit". Protokol jest liniowy: kazda wiadomosc konczy sie znakiem '\n'.
 *
 * KLIENT -> SERWER (komendy gracza):
 *   JOIN <nazwa>          - dolaczenie do gry, rejestracja gracza
 *   READY                 - przelaczenie statusu gotowosci w lobby
 *   START                 - zadanie rozpoczecia rundy (gdy wszyscy gotowi)
 *   MOVE UP|DOWN|LEFT|RIGHT - zadanie ruchu w danym kierunku
 *   BOMB                  - zadanie postawienia bomby na biezacym polu
 *   QUIT                  - dobrowolne opuszczenie gry
 *
 * SERWER -> KLIENT:
 *   WELCOME|<id>|<W>|<H>
 *       potwierdzenie dolaczenia, przydzielone id gracza i wymiary planszy
 *   STATE|<tick>|<phase>|<W>|<H>|<board>|<bombs>|<fires>|<players>
 *       pelny stan gry wysylany w kazdym ticku (autorytatywny snapshot)
 *         phase   : 0=WAITING, 1=PLAYING, 2=ROUNDOVER
 *         board   : W*H znakow (row-major):  '.' puste, '#' sciana, '+' skrzynka
 *         bombs   : tokeny "x,y,timer" rozdzielone spacja
 *         fires   : tokeny "x,y" rozdzielone spacja
 *         players : tokeny "id,x,y,alive,score,ready,name" rozdzielone spacja
 *       Puste pole (brak elementow) jest po prostu pustym ciagiem miedzy '|'.
 *   MSG|<tekst>
 *       komunikat informacyjny (np. zwyciezca rundy, remis)
 * --------------------------------------------------------------------------
 */

#define DEFAULT_PORT        5000

#define BOARD_W             15
#define BOARD_H             15
#define MAX_PLAYERS         4
#define MAX_BOMBS           64
#define MAX_NAME            32

/* Czestotliwosc glownej petli gry. 100 ms => 10 tickow na sekunde. */
#define TICK_MS             100

/* Bomba wybucha po ~3 sekundach (30 tickow * 100 ms). */
#define BOMB_TIMER_TICKS    30

/* Ogien utrzymuje sie ~0.5 s po wybuchu. */
#define FIRE_DURATION_TICKS 5

/* Domyslne parametry gracza. */
#define DEFAULT_RANGE       2
#define DEFAULT_MAX_BOMBS   1

/* Minimalna liczba podlaczonych graczy aby rozpoczac runde. */
#define MIN_PLAYERS         2

/* Czas trwania ekranu konca rundy (3 sekundy) przed restartem. */
#define ROUNDOVER_TICKS     30

/* Znaki terenu uzywane w serializacji planszy. */
#define CH_EMPTY 	    '.'
#define CH_WALL             '#'
#define CH_BOX              '+'

/* Maksymalny rozmiar bufora na zserializowany stan gry. */
#define STATE_BUF_SIZE      4096

#endif /* PROTOCOL_H */
