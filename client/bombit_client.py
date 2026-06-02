#!/usr/bin/env python3
"""
Bombit - cienki klient (PyGame).

Klient odpowiada WYLACZNIE za:
  * polaczenie TCP z serwerem autorytatywnym,
  * wysylanie asynchronicznych zadan gracza (RUCH / BOMBA),
  * odbieranie i renderowanie stanu planszy otrzymanego z serwera.

Cala logika gry (kolizje, wybuchy, eliminacje, punktacja) liczona jest
po stronie serwera w jezyku C. Klient nie podejmuje zadnych decyzji o stanie.

Ekrany klienta (maszyna stanow):
    MENU      -> wybor nazwy gracza, adresu i portu serwera
    LOBBY     -> lista podlaczonych graczy + przelacznik gotowosci + START
    PLAYING   -> wlasciwa rozgrywka
    GAMEOVER  -> ekran zwyciestwa / porazki / remisu + powtorzenie gry

Uruchomienie:
    python bombit_client.py [host] [port] [nazwa]   # argumenty sa opcjonalne
Sterowanie w grze:
    Strzalki / WASD - ruch
    Spacja          - postaw bombe
    ESC             - powrot do menu
"""

import sys
import socket
import threading
import pygame

# ----------------------------- Konfiguracja --------------------------------

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 5000
TILE = 40                     # rozmiar kafelka w pikselach
HUD_H = 70                    # wysokosc paska informacyjnego (HUD)
FPS = 60
MENU_W, MENU_H = 600, 670     # rozmiar okna w menu / na ekranie koncowym

# Kolory (styl retro-arcade).
COL_BG       = (24, 24, 32)
COL_PANEL    = (32, 34, 44)
COL_EMPTY    = (40, 44, 52)
COL_WALL     = (90, 96, 110)
COL_WALL_HI  = (120, 128, 145)
COL_BOX      = (170, 110, 60)
COL_BOX_HI   = (200, 140, 80)
COL_BOMB     = (20, 20, 20)
COL_BOMB_HI  = (60, 60, 60)
COL_FIRE     = (255, 140, 0)
COL_FIRE_HI  = (255, 220, 60)
COL_TEXT     = (235, 235, 235)
COL_MUTED    = (150, 155, 170)
COL_GRID     = (30, 32, 40)
COL_ACCENT   = (80, 170, 255)
COL_ERROR    = (255, 90, 90)
COL_WIN      = (90, 220, 120)
COL_LOSE     = (255, 90, 90)
COL_DRAW     = (240, 220, 80)
COL_BTN      = (52, 58, 74)
COL_BTN_HI   = (74, 84, 108)
COL_INPUT    = (44, 48, 60)
COL_INPUT_HI = (60, 110, 160)

# Kolory graczy wg id (0..3).
PLAYER_COLORS = [
    (80, 170, 255),   # niebieski
    (255, 90, 90),    # czerwony
    (90, 220, 120),   # zielony
    (240, 220, 80),   # zolty
]

PHASE_NAMES = {0: "OCZEKIWANIE", 1: "GRA", 2: "KONIEC RUNDY"}

# Stany klienta.
ST_MENU = "MENU"
ST_LOBBY = "LOBBY"
ST_PLAYING = "PLAYING"
ST_GAMEOVER = "GAMEOVER"


# --------------------------- Polaczenie sieciowe ---------------------------

class NetworkClient:
    """Obsluga gniazda TCP w osobnym watku odbiorczym."""

    def __init__(self, host, port, name):
        self.host = host
        self.port = port
        self.name = name
        self.sock = None
        self.running = False

        self.lock = threading.Lock()
        self.player_id = None
        self.board_w = 0
        self.board_h = 0
        self.state = None          # ostatni zdekodowany stan (dict)
        self.message = ""

    def connect(self):
        """Nawiazuje polaczenie. Rzuca OSError gdy serwer niedostepny."""
        self.sock = socket.create_connection((self.host, self.port), timeout=5)
        self.sock.settimeout(None)
        self.sock.sendall(f"JOIN {self.name}\n".encode())
        self.running = True
        threading.Thread(target=self._recv_loop, daemon=True).start()

    def _recv_loop(self):
        buf = ""
        try:
            while self.running:
                data = self.sock.recv(4096)
                if not data:
                    break
                buf += data.decode(errors="ignore")
                while "\n" in buf:
                    line, buf = buf.split("\n", 1)
                    self._handle_line(line.strip())
        except OSError:
            pass
        finally:
            self.running = False

    def _handle_line(self, line):
        if not line:
            return
        parts = line.split("|")
        tag = parts[0]

        if tag == "WELCOME":
            with self.lock:
                self.player_id = int(parts[1])
                self.board_w = int(parts[2])
                self.board_h = int(parts[3])
        elif tag == "STATE":
            state = self._parse_state(parts)
            if state:
                with self.lock:
                    self.state = state
        elif tag == "MSG":
            with self.lock:
                self.message = parts[1] if len(parts) > 1 else ""

    @staticmethod
    def _parse_state(parts):
        # STATE|tick|phase|W|H|board|bombs|fires|players
        if len(parts) < 9:
            return None
        try:
            tick = int(parts[1])
            phase = int(parts[2])
            w = int(parts[3])
            h = int(parts[4])
            board = parts[5]
            bombs = []
            for tok in parts[6].split():
                bx, by, bt = tok.split(",")
                bombs.append((int(bx), int(by), int(bt)))
            fires = []
            for tok in parts[7].split():
                fx, fy = tok.split(",")
                fires.append((int(fx), int(fy)))
            players = []
            for tok in parts[8].split():
                f = tok.split(",")
                players.append({
                    "id": int(f[0]),
                    "x": int(f[1]),
                    "y": int(f[2]),
                    "alive": int(f[3]),
                    "score": int(f[4]),
                    "ready": int(f[5]) if len(f) > 6 else 0,
                    "name": f[6] if len(f) > 6 else (f[5] if len(f) > 5 else "?"),
                })
        except (ValueError, IndexError):
            return None
        return {
            "tick": tick, "phase": phase, "w": w, "h": h,
            "board": board, "bombs": bombs, "fires": fires, "players": players,
        }

    def snapshot(self):
        """Zwraca (state, player_id, message) pod blokada."""
        with self.lock:
            return self.state, self.player_id, self.message

    def send(self, msg):
        if self.running and self.sock:
            try:
                self.sock.sendall((msg + "\n").encode())
            except OSError:
                self.running = False

    def close(self):
        self.running = False
        if self.sock:
            try:
                self.sock.sendall(b"QUIT\n")
            except OSError:
                pass
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None


# ------------------------------ Widgety GUI --------------------------------

class Button:
    def __init__(self, rect, text, font, base=COL_BTN, hi=COL_BTN_HI):
        self.rect = pygame.Rect(rect)
        self.text = text
        self.font = font
        self.base = base
        self.hi = hi

    def draw(self, surf, mouse_pos):
        hovered = self.rect.collidepoint(mouse_pos)
        color = self.hi if hovered else self.base
        pygame.draw.rect(surf, color, self.rect, border_radius=8)
        pygame.draw.rect(surf, COL_ACCENT if hovered else COL_MUTED,
                         self.rect, 2, border_radius=8)
        label = self.font.render(self.text, True, COL_TEXT)
        surf.blit(label, label.get_rect(center=self.rect.center))

    def hit(self, pos):
        return self.rect.collidepoint(pos)


class TextInput:
    def __init__(self, rect, font, text="", label="", max_len=20,
                 allow_space=False, digits_only=False):
        self.rect = pygame.Rect(rect)
        self.font = font
        self.text = text
        self.label = label
        self.max_len = max_len
        self.allow_space = allow_space
        self.digits_only = digits_only
        self.focused = False

    def handle_event(self, event):
        if event.type == pygame.MOUSEBUTTONDOWN:
            self.focused = self.rect.collidepoint(event.pos)
        elif event.type == pygame.KEYDOWN and self.focused:
            if event.key == pygame.K_BACKSPACE:
                self.text = self.text[:-1]
            elif event.key in (pygame.K_RETURN, pygame.K_TAB, pygame.K_ESCAPE):
                pass  # obsluga wyzej
            else:
                ch = event.unicode
                if ch and ch.isprintable() and len(self.text) < self.max_len:
                    if self.digits_only and not ch.isdigit():
                        return
                    if not self.allow_space and ch == " ":
                        return
                    self.text += ch

    def draw(self, surf, mouse_pos):
        if self.label:
            lbl = self.font.render(self.label, True, COL_MUTED)
            surf.blit(lbl, (self.rect.x, self.rect.y - 26))
        pygame.draw.rect(surf, COL_INPUT, self.rect, border_radius=6)
        border = COL_INPUT_HI if self.focused else COL_MUTED
        pygame.draw.rect(surf, border, self.rect, 2, border_radius=6)
        shown = self.text + ("|" if self.focused else "")
        txt = self.font.render(shown, True, COL_TEXT)
        surf.blit(txt, (self.rect.x + 10,
                        self.rect.y + (self.rect.height - txt.get_height()) // 2))


# ------------------------------- Renderowanie ------------------------------

def draw_tile_3d(surf, rect, base, hi):
    """Rysuje kafelek z prostym efektem 3D (jasniejsza krawedz)."""
    pygame.draw.rect(surf, base, rect)
    pygame.draw.rect(surf, hi, rect, 2)


def render_game(screen, font, big_font, net):
    screen.fill(COL_BG)
    state, my_id, message = net.snapshot()

    if state is None:
        text = big_font.render("Laczenie z serwerem...", True, COL_TEXT)
        screen.blit(text, text.get_rect(center=screen.get_rect().center))
        return

    w, h = state["w"], state["h"]
    board = state["board"]
    oy = HUD_H

    # Teren.
    for y in range(h):
        for x in range(w):
            ch = board[y * w + x] if y * w + x < len(board) else "."
            rect = pygame.Rect(x * TILE, oy + y * TILE, TILE, TILE)
            if ch == "#":
                draw_tile_3d(screen, rect, COL_WALL, COL_WALL_HI)
            elif ch == "+":
                draw_tile_3d(screen, rect, COL_BOX, COL_BOX_HI)
            else:
                pygame.draw.rect(screen, COL_EMPTY, rect)
                pygame.draw.rect(screen, COL_GRID, rect, 1)

    # Ogien.
    for (fx, fy) in state["fires"]:
        rect = pygame.Rect(fx * TILE, oy + fy * TILE, TILE, TILE)
        draw_tile_3d(screen, rect, COL_FIRE, COL_FIRE_HI)

    # Bomby.
    for (bx, by, bt) in state["bombs"]:
        cx = bx * TILE + TILE // 2
        cy = oy + by * TILE + TILE // 2
        r = TILE // 2 - 4 - (bt % 2) * 2     # pulsacja
        pygame.draw.circle(screen, COL_BOMB, (cx, cy), r)
        pygame.draw.circle(screen, COL_BOMB_HI, (cx, cy), r, 2)
        pygame.draw.circle(screen, COL_FIRE, (cx, cy - r), 3)

    # Gracze.
    for p in state["players"]:
        if not p["alive"]:
            continue
        color = PLAYER_COLORS[p["id"] % len(PLAYER_COLORS)]
        rect = pygame.Rect(p["x"] * TILE + 5, oy + p["y"] * TILE + 5,
                           TILE - 10, TILE - 10)
        pygame.draw.rect(screen, color, rect, border_radius=6)
        if p["id"] == my_id:
            pygame.draw.rect(screen, COL_TEXT, rect, 2, border_radius=6)

    # HUD: faza, komunikat, tablica wynikow.
    phase_txt = PHASE_NAMES.get(state["phase"], "?")
    hud = font.render(f"Faza: {phase_txt}    {message}", True, COL_TEXT)
    screen.blit(hud, (10, 8))

    sx = 10
    for p in sorted(state["players"], key=lambda q: q["id"]):
        color = PLAYER_COLORS[p["id"] % len(PLAYER_COLORS)]
        you = " (TY)" if p["id"] == my_id else ""
        dead = "" if p["alive"] else " [X]"
        label = font.render(f"{p['name']}{you}: {p['score']}{dead}", True, color)
        screen.blit(label, (sx, 36))
        sx += label.get_width() + 24


def compute_result(state, my_id):
    """Okresla wynik rundy z perspektywy gracza my_id."""
    if state is None:
        return "draw"
    alive = [p for p in state["players"] if p["alive"]]
    me = next((p for p in state["players"] if p["id"] == my_id), None)
    if me and me["alive"] and len(alive) == 1:
        return "victory"
    if len(alive) == 0:
        return "draw"
    return "defeat"


# ------------------------------ Ekran: MENU --------------------------------

class MenuScreen:
    def __init__(self, font, big_font, title_font, defaults):
        cx = MENU_W // 2
        self.title_font = title_font
        self.big_font = big_font
        self.font = font
        self.name_in = TextInput((cx - 150, 230, 300, 44), big_font,
                                 text=defaults["name"], label="Nazwa gracza",
                                 max_len=12)
        self.host_in = TextInput((cx - 150, 320, 180, 44), big_font,
                                 text=defaults["host"], label="Serwer (host)",
                                 max_len=24, allow_space=False)
        self.port_in = TextInput((cx + 50, 320, 100, 44), big_font,
                                 text=str(defaults["port"]), label="Port",
                                 max_len=5, digits_only=True)
        self.play_btn = Button((cx - 150, 420, 300, 56), "GRAJ", title_font,
                               base=(46, 120, 80), hi=(60, 160, 100))
        self.quit_btn = Button((cx - 150, 490, 300, 46), "Wyjdz", big_font)
        self.inputs = [self.name_in, self.host_in, self.port_in]
        self.error = ""

    def handle_event(self, event):
        for inp in self.inputs:
            inp.handle_event(event)
        if event.type == pygame.MOUSEBUTTONDOWN:
            if self.play_btn.hit(event.pos):
                return "play"
            if self.quit_btn.hit(event.pos):
                return "quit"
        if event.type == pygame.KEYDOWN and event.key == pygame.K_RETURN:
            return "play"
        return None

    def values(self):
        name = self.name_in.text.strip() or "Gracz"
        host = self.host_in.text.strip() or DEFAULT_HOST
        try:
            port = int(self.port_in.text)
        except ValueError:
            port = DEFAULT_PORT
        return name, host, port

    def draw(self, screen, mouse_pos):
        screen.fill(COL_BG)
        title = self.title_font.render("BOMBIT", True, COL_ACCENT)
        screen.blit(title, title.get_rect(center=(MENU_W // 2, 110)))
        sub = self.font.render("sieciowa gra typu Bomberman", True, COL_MUTED)
        screen.blit(sub, sub.get_rect(center=(MENU_W // 2, 160)))

        for inp in self.inputs:
            inp.draw(screen, mouse_pos)
        self.play_btn.draw(screen, mouse_pos)
        self.quit_btn.draw(screen, mouse_pos)

        if self.error:
            err = self.font.render(self.error, True, COL_ERROR)
            screen.blit(err, err.get_rect(center=(MENU_W // 2, 560)))

        hint = self.font.render("Wskazowka: uruchom 2+ klientow, runda startuje sama.",
                                True, COL_MUTED)
        screen.blit(hint, hint.get_rect(center=(MENU_W // 2, 620)))


# ------------------------------ Ekran: LOBBY ------------------------------

class LobbyScreen:
    """Lobby z lista graczy, przelacznikiem gotowosci i przyciskiem START."""

    def __init__(self, font, big_font, title_font):
        cx = MENU_W // 2
        self.font = font
        self.big_font = big_font
        self.title_font = title_font
        self.ready_btn = Button((cx - 250, 490, 240, 56), "Gotowy", big_font,
                                base=(80, 90, 110), hi=(110, 130, 160))
        self.start_btn = Button((cx + 10, 490, 240, 56), "START", title_font,
                                base=(46, 120, 80), hi=(60, 160, 100))
        self.leave_btn = Button((cx - 150, 570, 300, 44), "Opusc lobby",
                                big_font)

    def handle_event(self, event, all_ready, enough_players, me_ready):
        if event.type == pygame.MOUSEBUTTONDOWN:
            if self.ready_btn.hit(event.pos):
                return "ready"
            if self.start_btn.hit(event.pos) and all_ready and enough_players:
                return "start"
            if self.leave_btn.hit(event.pos):
                return "leave"
        if event.type == pygame.KEYDOWN:
            if event.key in (pygame.K_r, pygame.K_SPACE):
                return "ready"
            if event.key == pygame.K_RETURN and all_ready and enough_players:
                return "start"
            if event.key == pygame.K_ESCAPE:
                return "leave"
        return None

    def draw(self, screen, mouse_pos, snap_state, my_id, server_msg,
             min_players):
        screen.fill(COL_BG)
        title = self.title_font.render("LOBBY", True, COL_ACCENT)
        screen.blit(title, title.get_rect(center=(MENU_W // 2, 80)))

        players = snap_state["players"] if snap_state else []
        connected = len(players)
        ready = sum(1 for p in players if p["ready"])
        enough = connected >= min_players
        all_ready = enough and ready == connected
        me_ready = False
        for p in players:
            if p["id"] == my_id:
                me_ready = bool(p["ready"])
                break

        sub = self.font.render(
            f"Gotowych: {ready}/{connected}    Minimum graczy: {min_players}",
            True, COL_MUTED)
        screen.blit(sub, sub.get_rect(center=(MENU_W // 2, 130)))

        # Lista graczy.
        head = self.big_font.render("Gracze", True, COL_TEXT)
        screen.blit(head, head.get_rect(center=(MENU_W // 2, 180)))

        panel = pygame.Rect(60, 215, MENU_W - 120, 250)
        pygame.draw.rect(screen, COL_PANEL, panel, border_radius=10)
        pygame.draw.rect(screen, COL_MUTED, panel, 1, border_radius=10)

        if not players:
            empty = self.font.render(
                "Brak graczy. Poczekaj az ktos dolaczy...", True, COL_MUTED)
            screen.blit(empty, empty.get_rect(center=panel.center))
        else:
            y = panel.y + 16
            for p in sorted(players, key=lambda q: q["id"]):
                col = PLAYER_COLORS[p["id"] % len(PLAYER_COLORS)]
                # Kolorowy znacznik gracza.
                pygame.draw.rect(screen, col, (panel.x + 16, y + 4, 26, 26),
                                 border_radius=4)
                you = "  (TY)" if p["id"] == my_id else ""
                name_lbl = self.big_font.render(f"{p['name']}{you}", True,
                                                COL_TEXT)
                screen.blit(name_lbl, (panel.x + 56, y))

                status_text = "GOTOWY" if p["ready"] else "oczekuje..."
                status_color = COL_WIN if p["ready"] else COL_MUTED
                status_lbl = self.font.render(status_text, True, status_color)
                screen.blit(status_lbl,
                            (panel.right - 16 - status_lbl.get_width(), y + 8))
                y += 38

        # Komunikat z serwera.
        if server_msg:
            msg = self.font.render(server_msg, True, COL_MUTED)
            screen.blit(msg, msg.get_rect(center=(MENU_W // 2, 480)))

        # Przyciski.
        self.ready_btn.text = "Anuluj gotowosc" if me_ready else "Gotowy"
        self.ready_btn.base = (110, 90, 50) if me_ready else (80, 90, 110)
        self.ready_btn.hi = (150, 120, 70) if me_ready else (110, 130, 160)
        self.ready_btn.draw(screen, mouse_pos)

        # START aktywny tylko gdy wszyscy gotowi i jest >= MIN.
        if all_ready:
            self.start_btn.draw(screen, mouse_pos)
        else:
            pygame.draw.rect(screen, (50, 55, 65), self.start_btn.rect,
                             border_radius=8)
            pygame.draw.rect(screen, COL_MUTED, self.start_btn.rect, 2,
                             border_radius=8)
            lbl = self.title_font.render("START", True, COL_MUTED)
            screen.blit(lbl, lbl.get_rect(center=self.start_btn.rect.center))

        self.leave_btn.draw(screen, mouse_pos)


# --------------------------- Ekran: KONIEC GRY -----------------------------

class GameOverScreen:
    def __init__(self, font, big_font, title_font):
        cx = MENU_W // 2
        self.font = font
        self.big_font = big_font
        self.title_font = title_font
        self.again_btn = Button((cx - 150, 470, 300, 56), "Powrot do lobby",
                                title_font, base=(46, 120, 80), hi=(60, 160, 100))
        self.menu_btn = Button((cx - 150, 540, 300, 46), "Menu glowne", big_font)
        self.result = "draw"
        self.final_state = None
        self.my_id = None

    def set_result(self, result, state, my_id):
        self.result = result
        self.final_state = state
        self.my_id = my_id

    def handle_event(self, event):
        if event.type == pygame.MOUSEBUTTONDOWN:
            if self.again_btn.hit(event.pos):
                return "again"
            if self.menu_btn.hit(event.pos):
                return "menu"
        if event.type == pygame.KEYDOWN:
            if event.key == pygame.K_RETURN:
                return "again"
            if event.key == pygame.K_ESCAPE:
                return "menu"
        return None

    def draw(self, screen, mouse_pos):
        screen.fill(COL_BG)
        if self.result == "victory":
            text, color = "ZWYCIESTWO!", COL_WIN
        elif self.result == "defeat":
            text, color = "PORAZKA", COL_LOSE
        else:
            text, color = "REMIS", COL_DRAW

        title = self.title_font.render(text, True, color)
        screen.blit(title, title.get_rect(center=(MENU_W // 2, 130)))

        # Tablica wynikow.
        head = self.big_font.render("Tablica wynikow", True, COL_TEXT)
        screen.blit(head, head.get_rect(center=(MENU_W // 2, 230)))
        y = 280
        if self.final_state:
            ranking = sorted(self.final_state["players"],
                             key=lambda p: (-p["score"], p["id"]))
            for p in ranking:
                col = PLAYER_COLORS[p["id"] % len(PLAYER_COLORS)]
                you = "  (TY)" if p["id"] == self.my_id else ""
                line = self.big_font.render(
                    f"{p['name']}{you}", True, col)
                pts = self.big_font.render(f"{p['score']} pkt", True, COL_TEXT)
                screen.blit(line, (MENU_W // 2 - 150, y))
                screen.blit(pts, (MENU_W // 2 + 90, y))
                y += 40

        self.again_btn.draw(screen, mouse_pos)
        self.menu_btn.draw(screen, mouse_pos)


# --------------------------------- Main ------------------------------------

def main():
    defaults = {
        "name": sys.argv[3] if len(sys.argv) > 3 else "Gracz",
        "host": sys.argv[1] if len(sys.argv) > 1 else DEFAULT_HOST,
        "port": int(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_PORT,
    }

    pygame.init()
    pygame.display.set_caption("Bombit")
    screen = pygame.display.set_mode((MENU_W, MENU_H))
    clock = pygame.time.Clock()

    font = pygame.font.SysFont("consolas", 18)
    big_font = pygame.font.SysFont("consolas", 24)
    title_font = pygame.font.SysFont("consolas", 56, bold=True)

    menu = MenuScreen(font, big_font, title_font, defaults)
    lobby = LobbyScreen(font, big_font, title_font)
    gameover = GameOverScreen(font, big_font, title_font)
    MIN_PLAYERS_CLIENT = 2     # mirror MIN_PLAYERS z serwera

    state = ST_MENU
    net = None
    prev_phase = None      # faza z poprzedniej klatki - do wykrycia przejscia

    # Mapowanie klawiszy ruchu na kierunki serwera.
    move_keys = {
        pygame.K_UP: "UP", pygame.K_w: "UP",
        pygame.K_DOWN: "DOWN", pygame.K_s: "DOWN",
        pygame.K_LEFT: "LEFT", pygame.K_a: "LEFT",
        pygame.K_RIGHT: "RIGHT", pygame.K_d: "RIGHT",
    }
    move_cooldown = 120
    last_move = 0

    def resize_for_board():
        with net.lock:
            bw = net.board_w or 15
            bh = net.board_h or 15
        return pygame.display.set_mode((bw * TILE, bh * TILE + HUD_H))

    def start_connection():
        nonlocal net, state, screen, prev_phase
        name, host, port = menu.values()
        net = NetworkClient(host, port, name)
        try:
            net.connect()
        except OSError as e:
            menu.error = f"Nie mozna polaczyc z {host}:{port}"
            print(f"Blad polaczenia: {e}")
            net = None
            return
        menu.error = ""
        # Poczekaj chwile na WELCOME aby ustalic wymiary planszy.
        for _ in range(100):
            with net.lock:
                if net.board_w:
                    break
            pygame.time.delay(20)
        snap, _, _ = net.snapshot()
        prev_phase = snap["phase"] if snap else 0
        # Po polaczeniu trafiamy do lobby - okno zachowuje rozmiar menu.
        state = ST_LOBBY

    running = True
    while running:
        now = pygame.time.get_ticks()
        mouse_pos = pygame.mouse.get_pos()
        events = pygame.event.get()

        # --- Globalne zdarzenia ---
        for event in events:
            if event.type == pygame.QUIT:
                running = False

        # ===================== STAN: MENU =====================
        if state == ST_MENU:
            for event in events:
                action = menu.handle_event(event)
                if action == "play":
                    start_connection()
                elif action == "quit":
                    running = False
            menu.draw(screen, mouse_pos)

        # ===================== STAN: LOBBY ====================
        elif state == ST_LOBBY:
            snap_state, my_id, server_msg = net.snapshot() if net else (None, None, "")
            players = snap_state["players"] if snap_state else []
            connected = len(players)
            ready = sum(1 for p in players if p["ready"])
            enough = connected >= MIN_PLAYERS_CLIENT
            all_ready = enough and ready == connected
            me_ready = any(p["ready"] for p in players if p["id"] == my_id)

            for event in events:
                action = lobby.handle_event(event, all_ready, enough, me_ready)
                if action == "ready":
                    net.send("READY")
                elif action == "start":
                    net.send("START")
                elif action == "leave":
                    if net:
                        net.close()
                    net = None
                    screen = pygame.display.set_mode((MENU_W, MENU_H))
                    state = ST_MENU

            if state != ST_LOBBY:
                continue

            # Utrata polaczenia -> menu z komunikatem.
            if not net or not net.running:
                if net:
                    net.close()
                net = None
                menu.error = "Utracono polaczenie z serwerem."
                screen = pygame.display.set_mode((MENU_W, MENU_H))
                state = ST_MENU
                continue

            # Wykrycie startu rozgrywki: serwer przeszedl WAITING -> PLAYING.
            if snap_state and snap_state["phase"] == 1:
                prev_phase = 1
                screen = resize_for_board()
                state = ST_PLAYING
                continue

            lobby.draw(screen, mouse_pos, snap_state, my_id, server_msg,
                       MIN_PLAYERS_CLIENT)

        # =================== STAN: PLAYING ====================
        elif state == ST_PLAYING:
            for event in events:
                if event.type == pygame.KEYDOWN:
                    if event.key == pygame.K_ESCAPE:
                        # Powrot do lobby - polaczenie pozostaje aktywne.
                        screen = pygame.display.set_mode((MENU_W, MENU_H))
                        state = ST_LOBBY
                    elif event.key == pygame.K_SPACE:
                        net.send("BOMB")
                    elif event.key in move_keys:
                        net.send(f"MOVE {move_keys[event.key]}")
                        last_move = now

            if state != ST_PLAYING:
                continue

            # Plynny ruch przy przytrzymaniu klawisza.
            pressed = pygame.key.get_pressed()
            if now - last_move >= move_cooldown:
                for key, direction in move_keys.items():
                    if pressed[key]:
                        net.send(f"MOVE {direction}")
                        last_move = now
                        break

            # Utrata polaczenia -> powrot do menu z komunikatem.
            if not net or not net.running:
                if net:
                    net.close()
                net = None
                menu.error = "Utracono polaczenie z serwerem."
                screen = pygame.display.set_mode((MENU_W, MENU_H))
                state = ST_MENU
                continue

            render_game(screen, font, big_font, net)

            # Wykrycie PRZEJSCIA w faze konca rundy -> ekran wyniku.
            snap_state, my_id, _ = net.snapshot()
            if snap_state:
                cur_phase = snap_state["phase"]
                if cur_phase == 2 and prev_phase != 2:
                    gameover.set_result(compute_result(snap_state, my_id),
                                        snap_state, my_id)
                    screen = pygame.display.set_mode((MENU_W, MENU_H))
                    state = ST_GAMEOVER
                prev_phase = cur_phase

        # =================== STAN: GAMEOVER ===================
        elif state == ST_GAMEOVER:
            for event in events:
                action = gameover.handle_event(event)
                if action == "again":
                    # Po rundzie serwer wraca do WAITING i zeruje gotowosci -
                    # gracze musza ponownie nacisnac "Gotowy" w lobby.
                    if net and net.running:
                        snap, _, _ = net.snapshot()
                        prev_phase = snap["phase"] if snap else 0
                        screen = pygame.display.set_mode((MENU_W, MENU_H))
                        state = ST_LOBBY
                    else:
                        if net:
                            net.close()
                        net = None
                        menu.error = "Utracono polaczenie z serwerem."
                        state = ST_MENU
                elif action == "menu":
                    if net:
                        net.close()
                    net = None
                    state = ST_MENU
            gameover.draw(screen, mouse_pos)

        pygame.display.flip()
        clock.tick(FPS)

    if net:
        net.close()
    pygame.quit()


if __name__ == "__main__":
    main()
