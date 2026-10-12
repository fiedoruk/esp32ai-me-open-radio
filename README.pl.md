# Open Radio Core2

<p align="center"><img src="brand/logo-a-signal-cube.svg" width="92" alt="Open Radio — logo signal cube"></p>

<p align="center">
  <img src="https://img.shields.io/badge/wydane-0.4.4-3db8f5" alt="Wydane 0.4.4">
  <img src="https://img.shields.io/badge/licencja-GPL--3.0--or--later-blue" alt="GPL-3.0-or-later">
  <img src="https://img.shields.io/badge/stacje-236_wysylanych-success" alt="236 wysyłanych stacji">
</p>

<p align="center"><a href="https://esp32ai.me/pl/radio"><img src="https://esp32ai.me/assets/img/or/core2-1.jpg" width="740" alt="Open Radio grające na prawdziwej kostce M5Stack Core2"></a></p>

<p align="center">
  <img src="https://esp32ai.me/assets/img/or/screen-1.png" height="150" alt="Ekran odtwarzania">
  <img src="https://esp32ai.me/assets/img/or/screen-2.png" height="150" alt="Wybór stacji">
  <img src="https://esp32ai.me/assets/img/or/screen-3.png" height="150" alt="Ustawienia">
  <img src="https://esp32ai.me/assets/img/or/screen-4.png" height="150" alt="Karta statusu">
</p>

<p align="center"><b><a href="https://esp32ai.me/pl/radio/instalacja">⚡ Zainstaluj z przeglądarki</a></b> · <a href="https://fiedoruk.pl/os/open-radio-0-4-4.bin">Pobierz 0.4.4</a> · <a href="https://esp32ai.me/pl/radio/stacje">236 stacji</a></p>

> Wydane publicznie jako **Open Radio**. Opublikowanym wydaniem jest **0.4.4**
> (12.10.2026) — ostatnie wydanie linii 0.4: naprawia dwa błędy znalezione
> w 0.4.2 i sprawia, że wyłączony głośnik Bluetooth wraca w kilka sekund, a nie
> w kilka minut. Bez nowych funkcji, bez nowych stacji, ten sam katalog
> 236 stacji. Wersja 0.4.3 nie wyszła osobno; jej poprawki są w 0.4.4.
> Wcześniejsze wydania 0.1, 0.2, 0.2.1, 0.4, 0.4.1 i 0.4.2 zostają do pobrania
> jako wydania historyczne. Oficjalne binarki pochodzą wyłącznie z `fiedoruk.pl/os/` i są
> linkowane ze strony [esp32ai.me/pl/radio](https://esp32ai.me/pl/radio), a
> odpowiadające im źródła są publikowane obok każdej binarki na licencji GPL.

Open Radio powstaje z pomocą AI, pod ludzkim kierunkiem i na odpowiedzialność
autora: każde publiczne wydanie mówi wprost, na jakim sprzęcie je wgrano, jakie
sumy kontrolne ma artefakt i czego wciąż nie umie.

Open Radio Core2 to otwartoźródłowa, niezawodna kostka radia internetowego dla
M5Stack Core2. Odtwarza polskie stacje przez Wi-Fi na zapamiętanym głośniku
Bluetooth, a głośnik wbudowany jest automatycznym wyjściem awaryjnym.

[English version](README.md)

## Obietnica produktu

Włączasz i gra. Codzienne słuchanie nie wymaga konta, telefonu, chmury projektu
ani rutynowej aktualizacji. Telefon jest potrzebny tylko przy dodawaniu nowej
sieci Wi-Fi przez lokalny portal urządzenia.

Radio ma:

- pamiętać zatwierdzone sieci Wi-Fi i wybierać dostępną,
- łączyć ostatni głośnik Bluetooth,
- uruchamiać ostatnią działającą stację,
- przechodzić na głośnik wbudowany,
- odzyskiwać Wi-Fi i stream bez restartu,
- wyjaśniać problemy prostym, praktycznym językiem,
- zachowywać ostatni dobry katalog i konfigurację na urządzeniu.

Nie łączy się samoczynnie z obcymi ani otwartymi sieciami. Nie ma telemetrii,
konta, automatycznego OTA, nagrywania, backendu projektu ani rutynowych zdalnych
aktualizacji katalogu. Bieżący firmware urządzenia używa tylko opcjonalnej,
bezpośredniej synchronizacji czasu NTP. Każde przyszłe pobranie przybliżonej
lokalizacji lub pogody musi być ograniczone i nigdy nie może stać się zależnością
bootu ani grania.

## Co oznacza „Core2”

Core2 to docelowy model sprzętu: **M5Stack Core2**. Jest technicznym
doprecyzowaniem, nie musi być finalną marką dla użytkownika. Pozostawienie go w
nazwie roboczej uczciwie wskazuje granicę sprzętową do czasu wyboru nazwy.

## Bluetooth — uczciwa obietnica

Testowalny kontrakt to zgodność ze standardem Bluetooth Classic A2DP Sink i
kodekiem SBC. Granie nie wymaga AVRCP ani funkcji producenta. Głośniki wyłącznie
Bluetooth LE Audio są poza możliwościami oryginalnego ESP32. Nie będzie
deklaracji uniwersalnej zgodności przed fizycznymi testami.

**Znane ograniczenia w 0.4.4.** Głośnik Bluetooth, który został wyłączony,
wraca teraz w kilka sekund: w dziesięciu cyklach na RMF FM na opublikowanych
bajtach od 3 do 16 sekund od chwili, gdy głośnik zadzwonił do radia, w dziewięciu
cyklach na dziesięć w ciągu 10 sekund (w 0.4.2 od 13 sekund do około pięciu
minut). Zmierzyliśmy to na jednym głośniku; drugiego nie testowaliśmy. Głośnik
często nie odbiera pierwszego telefonu radia i powrót trwa wtedy około pięciu
sekund dłużej. Głośnik kostki może przycinać na RMF FM, dopóki nie gra
Bluetooth: dekodowanie strumienia 128 kb/s jest na tym sprzęcie tylko o jedną
czwartą szybsze od odtwarzania. Przez głośnik Bluetooth nie zmierzyliśmy żadnej
przerwy.

## Czujniki powietrza — uczciwa obietnica

Czujniki są opcjonalne i nic bez nich nie czeka ani nie migocze. Po wpięciu
obsługiwanego modułu do Portu A kostka pokazuje dwutlenek węgla, temperaturę,
wilgotność, ciśnienie oraz **względną** liczbę z elementu gazowego — względną,
bo mówi tylko, o ile powietrze odbiega od tego, do czego czujnik przywykł w tym
pomieszczeniu. Progi mają opublikowane źródła, a alerty domyślnie milczą.

Odczyty środowiskowe pochodzą z opcjonalnych czujników instalowanych przez
użytkownika i mają charakter informacyjny. To NIE jest czujnik czadu, dymu ani
ulatniającego się gazu: element gazowy odpowiada na wiele różnych substancji
jedną liczbą i nie potrafi rozpoznać żadnej z nich. Do tych zagrożeń załóż
certyfikowane czujki.

## Bieżący zakres stacji

Kostka ma dziewięć slotów na stacje i **tę dziewiątkę wybierasz sam z
wbudowanego katalogu 236 polskich stacji** — każda sprawdzona realnym
połączeniem i zmierzona pod obciążeniem — przy pierwszym uruchomieniu oraz w
dowolnej chwili później przez konsolę w telefonie (QR). Pełny katalog:
[esp32ai.me/pl/radio/stacje](https://esp32ai.me/pl/radio/stacje).

Fabryczna dziewiątka (gra, dopóki nie ułożysz własnej): RMF FM, Radio ZET,
TOK FM, Antyradio, Radio ESKA, RMF MAXX, Radio Plus, Złote Przeboje i
Meloradio.

Każda pozycja katalogu ma ścieżkę MP3 w publicznym torze źródłowym. AAC i HLS
zostają poza publiczną binarką do czasu osobnych bramek: prawnej, transportowej
i zasobowej. Projekt nie pośredniczy w transmisji, nie retransmituje i nie
nagrywa stacji ani nie sugeruje ich poparcia.

## Docelowy sprzęt

- M5Stack Core2 z ESP32-D0WDQ6-V3 — zwalidowany w terenie na rewizjach
  sprzętu v1.0 (AXP192), v1.1 (AXP2101) i v1.3 tym samym obrazem; każda
  nowa rewizja Core2 wchodzi do macierzy testów wydania, gdy tylko jest
  dostępna
- 16 MB flash i 8 MB PSRAM
- ekran dotykowy 320×240
- wbudowany głośnik I2S i bateria
- stałe zasilanie USB-C z powerbanku
- wyjście Bluetooth Classic A2DP Source

## Co naprawia 0.4.4

Dwa błędy znalezione w opublikowanym 0.4.2, powolny powrót głośnika Bluetooth
i poprawki z 0.4.3, które nie wyszły osobno. Poza nimi nie zmieniło się nic:
żadnych nowych funkcji, żadnych nowych stacji.

- **Brakująca ikona stacji mogła restartować radio przy każdym starcie.** Serwer
  ikon, który odpowiadał `200` z pustą treścią, sprawiał, że radio wywoływało
  metodę na pustym wskaźniku. Pusta odpowiedź oznacza teraz po prostu brak ikony.
- **Głośnik wraca w kilka sekund.** Naprawione są cztery przyczyny: radio nie
  czeka już na pełny bufor własnego głośnika, zanim sięgnie po Bluetooth; zamyka
  łącze otwarte przez głośnik i samo do niego dzwoni, a gdy głośnik nie odbierze,
  od razu dzwoni jeszcze raz; pięciosekundowy limit nie zrywa już działających
  połączeń; a łącze, które biblioteka Bluetooth zgubiła w trakcie dzwonienia,
  uruchamia teraz dźwięk, zamiast zostać pominięte.
- **Mniej ciszy na RMF.** Martwy adres serwera nie liczy się już jako awaria
  całej stacji. Radio po sekundzie próbuje następnego adresu RMF FM, a dłużej
  czeka dopiero wtedy, gdy zawiodą wszystkie.
- **Niektóre stacje grały z zakłóceniami.** Jedenaście adresów w katalogu
  odpowiada na zapytanie HTTP/1.1 w kawałkach (chunked), a znaczniki kawałków
  trafiały do danych dźwięku. Radio pyta teraz w HTTP/1.0.
- **Mniejsze poprawki.** Tytuł utworu rozdzielony między dwa odczyty z sieci nie
  jest już ucinany na łączeniu. Kod na ekranie konfiguracji i tytuł na ekranie
  głównym są pokazywane w całości. Przycisk kodu na ekranie konfiguracji to
  neutralny „Zmień kod”. Mały tekst i neutralne przyciski są lepiej widoczne
  w jasnym motywie. Stuknięcie parowania zatrzymuje szukanie adresów stacji.
  Uszkodzona kalibracja ciśnienia daje brak odczytu zamiast przepełnienia.
  Opublikowana binarka nie zawiera już ścieżki komputera, na którym ją
  zbudowano.

## Aktualny stan

**Publicznym wydaniem jest 0.4.4, opublikowane 12.10.2026.** Tego samego dnia
wgrano je z wyczyszczonej pamięci na Core2 v1.3: onboarding od zera, sparowany
głośnik, którego kostka wcześniej nie znała, dziesięć cykli wyłączenia
i włączenia głośnika na RMF FM z powrotem od 3 do 16 sekund od chwili, gdy
głośnik zadzwonił, zero panik i zero nieoczekiwanych restartów. Na tych bajtach
nie sprawdzono: dwóch godzin grania przez Bluetooth, aktualizacji z 0.4.2
z zachowaniem ustawień, drugiego głośnika ani stacji innych niż RMF FM.
Ograniczenia, które zostają otwarte, stoją wyżej, nie w przypisie.

Gotowe teraz:

- dokładne podglądy GUI 320×240 w motywie jasnym i ciemnym,
- pełny i minimalistyczny ekran grania,
- lokalne ustawienia, wygaszacze i ekran OFF,
- lokalny onboarding Wi-Fi z wieloma zabezpieczonymi profilami i ograniczonym recovery,
- wykonywalne resolvery MP3 dla wszystkich dziewięciu bieżących stacji,
- routing na głośnik wbudowany i automatyczne, niezależne od modelu wyszukiwanie
  urządzeń renderujących audio przez Bluetooth Classic, z jednym zatwierdzonym
  peerem zapamiętanym po adresie przez ograniczony flow parowania/połączenia,
- do ośmiu automatycznie wybieranych zatwierdzonych profili Wi-Fi oraz
  dwuetapowy pełny reset danych lokalnych,
- powtarzalne źródła firmware.

Zmierzone na linii wydania 0.2.1, która wciąż jest jedynym wydaniem z
dowodami z więcej niż jednej kostki:

- smoke z dnia wydania na Core2 v1.0 właściciela, niezależna instalacja
  z terenu na fabrycznie świeżym v1.1 i potwierdzenie klienta na v1.3 —
  cała linia sprzętu pokryta tym samym obrazem,
- wszystkie dziewięć logotypów stacji pobranych w jednym passie, pierwszy
  dźwięk ≈1,2 s od sieci, zapamiętany głośnik wraca w ≈3,1 s, zero
  underrunów na starcie,
- bateria, pomiar właściciela: około 2,5 godziny typowego grania, przy
  sprzyjających warunkach blisko trzech (pobór ~140 mA, w szczytach
  ~190 mA; przyciemnienie ekranu zmienia niewiele),
- Xiaomi Sound Pocket pozostaje referencyjnym głośnikiem Classic A2DP/SBC.

Na linii wydania pozostają otwarte:

- sprawy Bluetooth opisane wyżej: drugi głośnik, nieodebrany pierwszy telefon
  i przycinanie głośnika kostki na RMF FM,
- wymuszone cykle recovery (Wi-Fi, stream, głośnik) i długi soak endurance,
  wraz z zachowaniem SD i powerbanku,
- zgodność z kolejnymi głośnikami Bluetooth Classic A2DP/SBC,
- zapas DRAM podczas wybierania Bluetooth (dip ~5 KB pod obserwacją).

Publiczny tor MP3-only jest torem wydanym: 0.4.1, 0.4 i 0.2.1 odtwarzają się
bajt w bajt poza stemplem pochodzenia, o ile zachowa się udokumentowane
parametry budowania, a build 0.4.4 z opublikowanej paczki źródeł, poza tym
repozytorium, daje aplikację wydania bajt w bajt (patrz
[REPRODUCIBILITY.pl.md](REPRODUCIBILITY.pl.md)).
Punkty endurance wyżej pozostają otwarte i są wymienione wprost.

## Budowanie

Firmware buduje się w PlatformIO z katalogu `firmware/public-candidate`
(środowisko `core2-public-candidate`). Przypięty toolchain, zależności i to, jak
porównać własny build z opublikowanym wydaniem, opisuje
[REPRODUCIBILITY.pl.md](REPRODUCIBILITY.pl.md).

## Instrukcja użytkownika

Przeczytaj [USER-GUIDE.pl.md](USER-GUIDE.pl.md): pierwszy start, codzienne
sterowanie, ustawienia ekranu, recovery, prywatność i znane ograniczenia.

## Dokumentacja

- [USER-GUIDE.pl.md](USER-GUIDE.pl.md) — pierwszy start, codzienne sterowanie, znane ograniczenia
- [REPRODUCIBILITY.pl.md](REPRODUCIBILITY.pl.md) — przypięty toolchain i odtworzenie opublikowanego obrazu
- [NETWORK-ENDPOINTS.md](NETWORK-ENDPOINTS.md) — każdy adres, z którym urządzenie może się łączyć (po angielsku)
- [SECURITY.md](SECURITY.md) — jak zgłosić błąd bezpieczeństwa (po angielsku)
- [SUPPORT.pl.md](SUPPORT.pl.md) — gdzie zadać pytanie
- [CONTRIBUTING.pl.md](CONTRIBUTING.pl.md) i [CONTRIBUTOR-LICENCE.pl.md](CONTRIBUTOR-LICENCE.pl.md) — zasady współpracy
- [GOVERNANCE.md](GOVERNANCE.md) — kto o czym decyduje (po angielsku)

## Autorstwo i licencja

Founder i pierwotny autor: **Tomasz Fiedoruk — [fiedoruk.pl](https://fiedoruk.pl)**.

Kod jest na `GPL-3.0-or-later`, oryginalna dokumentacja i grafiki na
`CC BY-SA 4.0`, a dane katalogowe na `CC0-1.0`. Oficjalne logotypy stacji nie są
dołączane bez udokumentowanej zgody. Publiczne wydania firmware (0.1, 0.2,
0.2.1, 0.4, 0.4.1, 0.4.2 i aktualne 0.4.4) pochodzą z `fiedoruk.pl/os/`; archiwum
odpowiadających źródeł jest publikowane obok każdej binarki, a każde wydanie ma
proweniencję zapisaną w manifeście wydania. Szczegóły:
[LICENSES.md](LICENSES.md), [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)
i [TRADEMARKS.md](TRADEMARKS.md).
