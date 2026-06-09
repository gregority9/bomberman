# Bombit — sieciowa gra typu Bomberman (klient-serwer)

Wieloosobowa (2–4 graczy) gra zręcznościowa 2D w architekturze **klient-serwer**:

- **Serwer** — autorytatywny, napisany w **czystym C** z użyciem **POSIX Threads**
  i gniazd **TCP**. Przechowuje cały stan gry, liczy logikę, kolizje, wybuchy
  i eliminacje, zarządza rundami i punktacją.
- **Klient** — „cienki klient" w **Pythonie + PyGame**. Wyłącznie renderuje stan
  otrzymany z serwera oraz wysyła asynchroniczne polecenia gracza (`MOVE`, `BOMB`).

## Struktura projektu

```
Bomberman/
├── server/                 # serwer w C
│   ├── include/
│   │   ├── protocol.h      # stałe + definicja protokołu tekstowego TCP
│   │   ├── queue.h         # bezpieczna wątkowo kolejka poleceń
│   │   ├── game.h          # struktury stanu gry + logika
│   │   └── server.h        # warstwa sieciowa (wątki, broadcast)
│   ├── src/
│   │   ├── queue.c         # kolejka zdarzeń (pthread_mutex_t)
│   │   ├── game.c          # logika: plansza, ruch, bomby, wybuchy, rundy
│   │   ├── server.c        # gniazdo TCP, wątki klientów, rozgłaszanie
│   │   └── main.c          # główna pętla gry (tick-rate)
│   └── Makefile
├── client/
│   ├── bombit_client.py    # klient PyGame
│   └── requirements.txt
└── README.md
```

## Architektura współbieżności

Serwer korzysta z kilku wątków i chronionych sekcji krytycznych:

1. **Wątek `accept`** — przyjmuje nowe połączenia TCP.
2. **Wątek na każdego klienta** — odbiera komendy, parsuje je i wkłada do
   wspólnej **kolejki poleceń** (`CommandQueue`).
3. **Główny wątek gry** — w równych odstępach (tick = 100 ms, 10 Hz) pobiera
   polecenia z kolejki, aktualizuje stan gry i rozgłasza go do klientów.

Sekcje krytyczne zabezpieczone `pthread_mutex_t`:

| Zasób współdzielony            | Mutex                     |
|--------------------------------|---------------------------|
| Kolejka poleceń (event queue)  | `CommandQueue.mutex`      |
| Stan gry / plansza / gracze    | `GameState.mutex`         |
| Liczniki aktywnych bomb gracza | `GameState.mutex` (część) |
| Rejestr gniazd klientów        | `Server.clients_mutex`    |

Cały tick liczony jest pod jednym zamkiem `GameState.mutex`, dzięki czemu
konflikty (np. dwóch graczy wchodzących na to samo pole) rozstrzygane są
deterministycznie według kolejności poleceń w kolejce.

## Wymagania

- **Serwer:** Linux / WSL / MSYS2 z `gcc` i `make` (kod używa POSIX sockets
  i pthreads — nie kompiluje się natywnym MSVC na Windows).
- **Klient:** Python 3.8+ oraz `pygame` (`pip install -r client/requirements.txt`).

## Budowanie i uruchomienie serwera

```bash
cd server
make                 # -> build/bombit_server
./build/bombit_server         # port domyślny 5000
./build/bombit_server 6000    # własny port
```

Na Windowsie najwygodniej przez WSL:

```bash
wsl
cd /mnt/c/Users/.../Bomberman/server
make && ./build/bombit_server
```

## Uruchomienie klienta

```bash
cd client
pip install -r requirements.txt
python bombit_client.py [host] [port] [nazwa]
# np.
python bombit_client.py 127.0.0.1 5000 Alice
```

Uruchom **co najmniej dwóch** klientów. Po dołączeniu trafiają oni do
**lobby**, gdzie każdy musi nacisnąć **Gotowy**. Gdy wszyscy są gotowi
i jest ≥2 graczy, przycisk **START** się odblokowuje — jego naciśnięcie
wysyła do serwera polecenie rozpoczęcia rundy (wystarczy, że START
naciśnie jeden z graczy).

### Ekrany klienta

Klient działa jako maszyna stanów:

- **MENU** — wybór nazwy gracza, adresu serwera i portu, przycisk **GRAJ**.
- **LOBBY** — lista podłączonych graczy, przycisk **Gotowy / Anuluj gotowość**,
  zablokowany przycisk **START**, który odblokowuje się dopiero gdy wszyscy
  oznaczyli gotowość. Naciśnięcie START informuje serwer, że rozpoczynamy.
- **GRA** — właściwa rozgrywka z HUD-em i tablicą wyników.
- **KONIEC GRY** — ekran **ZWYCIĘSTWO / PORAŻKA / REMIS** z tablicą wyników
  oraz przyciskami **Powrót do lobby** (każdy musi ponownie oznaczyć gotowość
  do nowej rundy) i **Menu główne** (rozłączenie).

### Sterowanie

| Klawisz                | Akcja                                       |
|------------------------|---------------------------------------------|
| Strzałki / `WASD`      | ruch                                        |
| `Spacja`               | postaw bombę (w grze) / przełącz gotowość (w lobby) |
| `R`                    | przełącz gotowość (w lobby)                 |
| `Enter`                | GRAJ / START / powrót do lobby              |
| `ESC`                  | powrót do menu / lobby                      |
| zamknięcie okna        | wyjście                                     |

## Zasady gry

- Plansza 15×15: ściany niezniszczalne (`#`), skrzynki zniszczalne (`+`),
  pola puste (`.`). Gracze startują w narożnikach.
- Bomba wybucha po ~3 s w kształcie krzyża (domyślny zasięg 2). Fala niszczy
  skrzynki i eliminuje graczy w zasięgu; wybuchy się łańcuchują.
- Nie można wejść na ścianę, skrzynkę ani aktywną bombę.
- Cel: zostać ostatnim żywym graczem. Zwycięzca dostaje punkt; gdy zginą
  wszyscy naraz — **remis** (bez punktu). Po rundzie plansza się resetuje.

## Protokół (TCP, tekstowy, linie zakończone `\n`)

**Klient → serwer:**
`JOIN <nazwa>`, `READY`, `START`, `MOVE UP|DOWN|LEFT|RIGHT`, `BOMB`, `QUIT`

**Serwer → klient:**

```
WELCOME|<id>|<W>|<H>
STATE|<tick>|<phase>|<W>|<H>|<board>|<bombs>|<fires>|<players>
MSG|<tekst>
```

gdzie `phase` to `0`=lobby/oczekiwanie, `1`=gra, `2`=koniec rundy; `board` to
`W*H` znaków terenu (row-major); `bombs` = tokeny `x,y,timer`; `fires` =
tokeny `x,y`; `players` = tokeny `id,x,y,alive,score,ready,nazwa`.

Logika startu rundy: serwer pozostaje w `WAITING`, dopóki nie otrzyma
komendy `START` w ticku, w którym **wszyscy** podłączeni gracze mają
`ready=1` i jest ich ≥ `MIN_PLAYERS`. Po zakończeniu rundy serwer
automatycznie wraca do `WAITING` i zeruje wszystkie flagi `ready` — każdy
gracz musi ponownie wcisnąć **Gotowy**.
