# Open Radio Core2 — instrukcja użytkownika

[English version](USER-GUIDE.md)

## Bieżące dowody sprzętowe

Aktualne publiczne wydanie to **0.4.4** (12.10.2026) — ostatnie wydanie linii
0.4. Naprawia dwa błędy znalezione w 0.4.2 i sprawia, że wyłączony głośnik
Bluetooth wraca w kilka sekund, a nie w kilka minut; nie dodaje funkcji ani
stacji. Jego dowody pochodzą z jednej sztuki — Core2 v1.3 właściciela — na
opublikowanym obrazie fabrycznym, wgranym z pełnego kasowania tak, jak instaluje
go kupujący: onboarding od zera, dźwięk z wbudowanego głośnika, sparowany
głośnik, którego kostka wcześniej nie znała, oraz dziesięć cykli wyłączenia
i włączenia głośnika na RMF FM z powrotem od 3 do 16 sekund od chwili, gdy
głośnik zadzwonił, w dziewięciu cyklach na dziesięć w ciągu 10 sekund; ani
chwili ciszy w głośniku, ani jednego przycięcia w głośniku kostki, zero panik
i zero nieoczekiwanych restartów. Lista znanych ograniczeń niżej obejmuje także ścieżki, których na
sprzęcie w ogóle nie sprawdzono.

**0.2.1 pozostaje wydaniem z najszerszym dowodem sprzętowym**: smoke z dnia
wydania na Core2 v1.0 właściciela, zewnętrzny użytkownik, który niezależnie
zainstalował i potwierdził je na fabrycznie świeżym v1.1, oraz klient, który
potwierdził v1.3 — cała linia rewizji sprzętu ma dowody z terenu dla tamtego
wydania, a dla 0.4.4 jeszcze nie. Czas na baterii jest zmierzony przez
właściciela (~2,5 h typowo, do ~3 h); praca z powerbanku i długie biegi
endurance pozostają niezmierzone.

## Pierwszy start

1. Podłącz radio do zasilania USB-C.
2. Jeśli nie ma zapisanej zatwierdzonej sieci Wi-Fi, połącz telefon z tymczasową
   siecią `OpenRadio-Setup`, używając jednorazowego kodu WPA2 pokazanego na radiu.
   Kod zajmuje na ekranie dwie linie: wpisz go jednym ciągiem, bez spacji,
   wielkimi literami (`OR-` i dwanaście znaków).
   Portal lokalny powinien otworzyć się sam; jeśli nie, otwórz `192.168.4.1`.
3. Dodaj zabezpieczoną sieć Wi-Fi. Radio nigdy samo nie wybiera obcej lub otwartej sieci.
4. Najpierw uruchamia się głośnik wbudowany, aby dźwięk nie zależał od Bluetooth.
5. Radio łączy zapamiętany głośnik Bluetooth Classic A2DP/SBC. Jeśli nie ma
   zapamiętanego głośnika, automatycznie zaczyna standardowy skan urządzeń audio;
   lokalny ekran Bluetooth pozwala go powtórzyć.

Po konfiguracji telefon nie jest potrzebny, dopóki nie dodajesz nowej sieci.

## Codzienny ekran

Dolne przyciski przełączają na poprzednią stację, otwierają siatkę stacji i
przechodzą do następnej wspieranej stacji. Przycisk ustawień otwiera preferencje.

Dostępne są dwa układy ekranu głównego:

- **Pełny** eksponuje wygląd stacji i aktualny tytuł ICY, jeśli jest dostępny.
- **Minimalny** eksponuje zegar i celowo ogranicza informacje o stacji.

Wybór zapisuje się na urządzeniu. Brak metadanych nigdy nie zatrzymuje dźwięku —
odpowiednie pole przechodzi w fallback albo znika.

## Ustawienia

Ustawienia są podzielone na trzy czytelnie ponumerowane strony:

- **Szybkie:** status Wi-Fi, Bluetooth, głośność, jasność, motyw i język.
- **System:** lokalizacja/region, ekran, informacje o projekcie, diagnostyka,
  układ ekranu głównego i ponowne skanowanie Bluetooth.
- **Ekran:** wygaszacz ON/OFF, czas, tryb, ekran OFF ON/OFF, czas i podgląd.

Karty mają pojedyncze, ograniczone akcje. Wartości przechodzą przez krótką listę;
nie ma ukrytej konfiguracji tekstowej ani potrzeby edycji kodu.

## Szum lokalny

W Ustawieniach dotknij ikony koncentrycznych okręgów obok **X**, aby otworzyć
w pełni lokalny tryb Szum. Duży przycisk uruchamia i zatrzymuje dźwięk, a niżej
wybierasz Biały, Różowy lub Brązowy; przyciski A/C zmieniają kolor, natomiast B
lub **X** wraca do ostatniej stacji. Kostka zapisuje wybrany tryb i kolor,
wznawia Szum po restarcie oraz zachowuje to samo wyjście Bluetooth i fallback
na głośnik wbudowany bez uzależniania grania od Wi-Fi.

## Czujniki powietrza (opcjonalne)

Kostka działa bez żadnego czujnika i nic w niej nie czeka na moduł. Jeśli
jednak wepniesz obsługiwany moduł do Portu A, dochodzą dwa dodatkowe układy
ekranu głównego: radio z powietrzem pod spodem oraz powietrze z radiem
sprowadzonym do znaku. Przełączasz je tym samym kafelkiem w Ustawieniach, co
układ podstawowy. Bez modułu te dwa układy są niedostępne, bo pokazywałyby pustą
połowę ekranu — a Twój zapisany wybór czeka nietknięty i wraca, gdy moduł
wróci.

Pokazujemy dwutlenek węgla, temperaturę, wilgotność względną, ciśnienie oraz
liczbę z elementu gazowego. Ta ostatnia jest **względna**: mówi, o ile powietrze
odbiega od tego, do czego czujnik przywykł w tym pomieszczeniu, i niczego nie
nazywa. Po włączeniu modułu gazowego kostka przez pewien czas sama się wygrzewa
i mówi o tym wprost, zamiast pokazywać liczbę, której jeszcze nie umie odnieść.

Alerty są domyślnie ciche i mają histerezę, żeby nie migotały wokół progu.
Progi pochodzą z opublikowanych źródeł, a nie z naszego wyczucia.

Odczyty środowiskowe pochodzą z opcjonalnych czujników instalowanych przez
użytkownika i mają charakter informacyjny. To NIE jest czujnik czadu, dymu ani
ulatniającego się gazu: element gazowy odpowiada na wiele różnych substancji
jedną liczbą i nie potrafi rozpoznać żadnej z nich. Do tych zagrożeń załóż
certyfikowane czujki.

## Usypianie ekranu

Pulse, Bars, Orbit i Kiara są wygaszaczami wizualnymi. Włączają się po 30 sekundach do
10 minut. Ekran może wyłączyć się po 15, 30 albo 60 minutach. Audio, sieć i
recovery działają dalej. Dotyk budzi ekran i wraca do wybranego układu głównego.

## Zmiana Wi-Fi i dostęp do konsoli (zmienione w 0.4.2)

Sieć Wi-Fi zmienia się z własnego punktu dostępowego radia, a nie przez sieć
domową. Na ekranie Wi-Fi dolny przycisk otwiera ten punkt, gdy radio jest
połączone, a gdy zapisana sieć naprawdę zniknęła, portal otwiera się sam. Jeśli
masz otwartą sesję konsoli, zamknij ją najpierw: dopóki trwa, ten sam przycisk
znaczy „zamknij konsolę".

Konsola odpowiada pod dwoma adresami, które radio samo pokazuje — `http://open-radio.local`
oraz adres IP wypisany na ekranie Wi-Fi i zakodowany w tamtejszym kodzie QR.
Zakładka z nazwą, którą urządzeniu nadał router, dostanie krótką odpowiedź w
formacie JSON zamiast strony. Używaj adresu z ekranu.

Obie zmiany są zamierzone. Do 0.4.2 dowolna maszyna w tej samej sieci mogła
przestawić radio na inny punkt dostępowy w oknie konsoli.

## Trend ciśnienia i dlaczego strzałka wcześniej nie drgała

Strzałka obok odczytu ciśnienia i towarzyszące jej alerty potrzebują trzech
godzin historii, żeby cokolwiek powiedzieć. Do 0.4.2 pierścień trzymający tę
historię był o jedną próbkę krótszy niż trzy godziny, więc strzałka nie mogła
się zmienić niezależnie od tego, jak długo radio pracowało — właściciel zgłosił
dokładnie to po dziesięciu godzinach. Teraz może. Pierwsza strzałka nadal pojawi
się trzy godziny po zimnym starcie, bo tyle trwa odpowiedź na to pytanie.

## Recovery

- Brak Wi-Fi: radio ponawia znane sieci z ograniczonym backoffem.
- Zerwany stream: radio ponownie otwiera stream i obraca dostępne kandydaty.
  Stabilny dwell, scoring i kwarantanna per-endpoint nie są jeszcze wdrożone.
- Brak Bluetooth: audio przechodzi na głośnik wbudowany, a reconnect jest ograniczony.
- Uszkodzona konfiguracja: safe mode unika obcych sieci i proponuje lokalną naprawę.

## Prywatność

Nie ma konta, analityki ani backendu projektu. Dane Wi-Fi, identyfikatory znanych
sieci i diagnostyka pozostają na urządzeniu. Bieżący firmware używa opcjonalnej,
bezpośredniej synchronizacji czasu NTP. Planowane dodatki lokalizacji i pogody
nie istnieją w firmware urządzenia i nigdy nie mogą stać się zależnością grania.

## Znane ograniczenia

- **Powrót głośnika zmierzyliśmy na jednym głośniku.** Na opublikowanych
  bajtach dziesięć cykli wyłączenia i włączenia na RMF FM dało powrót od 3 do
  16 sekund od chwili, gdy głośnik zadzwonił. Drugiego głośnika nie
  testowaliśmy.
- Głośnik często nie odbiera pierwszego telefonu radia, bo w tej chwili sam
  jeszcze dzwoni do radia. Drugi telefon przechodzi, a powrót trwa wtedy około
  pięciu sekund dłużej.
- Raz na dwadzieścia cykli radio wstrzymało dźwięk na pięć sekund, bo pamięć
  była zbyt poszatkowana, żeby bezpiecznie uruchomić dźwięk Bluetooth, po czym
  zadzwoniło jeszcze raz: 23 sekundy.
- **Głośnik kostki może przycinać na RMF FM**, dopóki nie gra Bluetooth. Przez
  głośnik Bluetooth nie zmierzyliśmy żadnej przerwy.
- Gdy łącze z głośnikiem wstaje, strumień stacji czasem rusza od nowa na
  następnym serwerze, w około sekundę. Ciszy ani przycięcia nie zmierzyliśmy.
- Otwieranie strumienia pod adresem, który nie odpowiada, zamraża ekran do
  pięciu sekund, raz na każdy martwy adres.
- Dwóch godzin grania przez Bluetooth, aktualizacji, która zapisuje samą
  aplikację na 0.4.2 z zachowaniem ustawień, ani stacji innych niż RMF FM nie
  sprawdzono na opublikowanych bajtach 0.4.4.
- Historyczne notki niżej o oblanym 60-minutowym biegu dotyczą starszego,
  przedwydaniowego obrazu labowego, nie wydanego 0.2.1.
- Wymuszone cykle recovery Wi-Fi/streamu/głośnika, zachowanie PMU/baterii/SD
  i dłuższe endurance pozostają dla 0.2.1 formalnie niezakwalifikowane.
- Wszystkie dziewięć stacji wybranych z katalogu 236 używa ścieżek MP3, ale dostępność
  endpointów może zmieniać się niezależnie od jakiegokolwiek wydania.
- Oficjalne logotypy stacji nie są publicznie dołączone bez zgody.
- Głośniki wyłącznie Bluetooth LE Audio są poza kontraktem Core2.
- Aktualnym publicznym wydaniem jest 0.4.4; endurance i czas na baterii nie
  mają jeszcze formalnej kwalifikacji, a dowody sprzętowe 0.4.4 pochodzą z jednej sztuki.
