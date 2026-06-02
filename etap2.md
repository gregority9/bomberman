# Analiza zagrożeń spójności stanu aplikacji i propozycje przeciwdziałania

**Projekt:** Rozproszona, wieloosobowa gra zręcznościowa "Bombit" - wariacja gry "Bomberman"

## Skład grupy:

* Wiktor Tężycki (203271)
* Grzegorz Horbaczewski (203316)
* Kamil Łapacz (203270)

### 1. Krótki opis

Gra "Bombit" to sieciowa gra zręcznościowa 2D typu arena survival, inspirowana klasycznymi produkcjami w stylu Bomberman. Rozgrywka odbywa się w czasie rzeczywistym i przeznaczona jest dla od 2 do 4 graczy. Gracze poruszają się po planszy podzielonej na pola (siatce), unikając zagrożeń, niszcząc przeszkód i podkładając bomby w celu eliminacji przeciwników. Projekt opiera się na twardej architekturze klient-serwer, w której serwer pełni rolę jednostki autorytatywnej. Oznacza to, że przechowuje on główny stan gry, przelicza logikę i obsługuje kolizje, podczas gdy klienci odpowiadają jedynie za wyświetlanie planszy i wysyłanie poleceń. Zwycięstwo osiąga gracz, który jako jedyny pozostanie żywy na planszy, unikając eksplozji i skutecznie zastawiając pułapki. Rozgrywka dzieli się na krótkie rundy, za wygranie których przyznawany jest punkt dopisywany do tabeli wyników utrzymywanej przez serwer przez całą sesję.

### 2. Diagram Sekwencyjny

Poniżej przedstawiony jest opis sekwencji obrazujący sposób komunikacji między graczami (klientami) a serwerem w trakcie gry. Połączenie i komunikacja realizowane są przy użyciu gniazd TCP. Gracze łączą się z serwerem, a po rozpoczęciu rundy asynchronicznie przesyłają pakiety z poleceniami sterującymi (np. komendy **RUCH** lub **BOMBA** w formacie tekstowym/JSON). Wątki serwera odbierają te akcje i natychmiast umieszczają je we wspólnej kolejce poleceń.

W regularnych odstępach czasu (tickach) główny wątek serwera pobiera zdarzenia z kolejki i aplikuje je do logiki gry. Serwer weryfikuje poprawność każdej akcji (np. limity bomb gracza, czy pole nie jest zablokowane) i rozstrzyga ewentualne spory wynikające z jednoczesnych działań różnych klientów. Następnie serwer weryfikuje czas do wybuchu ładunków, generuje fale uderzeniowe i ewentualnie eliminuje graczy, którzy znaleźli się w zasięgu ognia. Po przeliczeniu całej logiki, serwer wysyła zaktualizowany stan planszy do wszystkich klientów. W międzyczasie serwer stale sprawdza, czy runda dobiegła końca. Jeśli przy życiu pozostanie tylko jeden gracz, serwer ogłasza koniec rundy, przyznaje punkt zwycięzcy i resetuje planszę do następnej rozgrywki.

![](data:image/png;base64...)

### 3. Diagram Klas

Najważniejszą częścią wieloosobowej gry "Bombit" jest asynchroniczna komunikacja w architekturze klient-serwer. Serwer (klasa/moduł **Server**) pełni rolę absolutnego nadzorcy (jednostki autorytatywnej), odbierając pakiety z komendami od graczy i zarządzając głównym stanem gry. Klient (klasa **Client**) zajmuje się przesyłaniem poleceń oraz renderowaniem zaktualizowanej planszy (np. za pomocą biblioteki PyGame).

Stan gry operuje na planszy przechowywanej w postaci dwuwymiarowej tablicy lub zbliżonej struktury danych (klasa **Board** / **Map**), która agreguje informacje o układzie siatki. Na planszy rozmieszczone są poszczególne pola (**Tile**), na których mogą znajdować się różne obiekty gry: **Wall** (ściany niezniszczalne), **Box** (skrzynki zniszczalne), **Bomb** (podłożone ładunki), **Fire** (ogień po wybuchu) oraz **Player** (gracze). Klasa Gracza przechowuje szczegółowe informacje na jego temat, ze szczególnym uwzględnieniem licznika aktywnych bomb, z którym serwer musi się komunikować przed dopuszczeniem żądania postawienia nowego ładunku.

Kluczowym elementem architektonicznym systemu jest **EventQueue** (Kolejka zdarzeń), wykorzystywana jako bufor komunikacyjny. Ponieważ do współdzielonych zasobów gry odwołują się jednocześnie wątki nasłuchujące sieci oraz główny wątek logiczny, kolejka poleceń stanowi sekcję krytyczną. Odczyt i zapis poleceń, a także aktualizacje stanu samej planszy, muszą być ściśle zabezpieczone mechanizmami synchronizacji (np. muteksami z biblioteki POSIX), aby uniknąć błędów wynikających z niespójnego stanu i zagwarantować integralność rozgrywki.

![](data:image/png;base64...)
