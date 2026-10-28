# Open Radio Core2 — instrukcja użytkownika

[English version](USER-GUIDE.md)

## Bieżące dowody sprzętowe

Aktualne publiczne wydanie to **0.4.5** (28.10.2026), odebrane z wyjątkami.
Usuwa kilka przyczyn przerywania dźwięku przy nierównym dopływie danych stacji,
problem pamięci, który po wielokrotnym łączeniu mógł zostawić radio w ciszy,
restart podczas utraty Wi-Fi i nieograniczone oczekiwanie przed uruchomieniem
Bluetooth; nie dodaje funkcji ani stacji. Jego dowody pochodzą z jednej sztuki
— Core2 v1.3 właściciela — z aplikacją wydania wgraną jako aktualizacja z 0.4.4,
która zachowała ustawienia: dwie godziny przez głośnik Bluetooth bez ramki
ciszy, zatrzymania strumienia i restartu; w ciągu 21 godzin w nocy dwa restarty
przy ponownym łączeniu Bluetooth, z powrotem po 129 i 83 sekundach, a potem
12,8 godziny bez restartu; dziesięć cykli zasilania głośnika, z których osiem
niezakłóconych wróciło w 3,2–16,5 sekundy. Lista znanych ograniczeń
niżej obejmuje także ścieżki, których na sprzęcie w ogóle nie sprawdzono.

**0.2.1 pozostaje wydaniem z najszerszym dowodem sprzętowym**: smoke z dnia
wydania na Core2 v1.0 właściciela, zewnętrzny użytkownik, który niezależnie
zainstalował i potwierdził je na fabrycznie świeżym v1.1, oraz klient, który
potwierdził v1.3 — cała linia rewizji sprzętu ma dowody z terenu dla tamtego
wydania, a dla 0.4.5 jeszcze nie. Czas na baterii jest zmierzony przez
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

### Ręczne otwieranie konfiguracji Wi-Fi w 0.4.5

Po ukończeniu konfiguracji i zapisaniu co najmniej jednej sieci utrata Wi-Fi
nie otwiera już automatycznie `OpenRadio-Setup`. Radio nadal próbuje zapisanych
sieci. Jeśli zmieniło się hasło lub radio trafiło do innej sieci:

1. Otwórz menu ustawień ikoną w prawym górnym rogu ekranu głównego.
   Na pierwszej stronie ustawień dotknij **Wi-Fi**.
2. Dotknij **Dodaj sieć Wi-Fi**. Jeśli trwa sesja konsoli, najpierw dotknij
   **Zamknij konsolę**, a następnie **Dodaj sieć Wi-Fi**.
3. Zeskanuj QR telefonem albo połącz się z `OpenRadio-Setup`, używając
   wyświetlonego kodu z pola **Kod awaryjny**. Jeśli strona konfiguracji
   nie otworzy się sama, otwórz `192.168.4.1`.
4. Wybierz swoją zabezpieczoną sieć i wpisz hasło. Po połączeniu radio zamknie
   sieć konfiguracji. **Zmień kod** tworzy nowe hasło konfiguracji; użyj tego
   przycisku tylko wtedy, gdy chcesz zastąpić wyświetlany kod.

W angielskim interfejsie przyciski to **Add Wi-Fi network**, **Close console**
i **Change code**, a **Kod awaryjny** to **Backup code**.
Automatyczna konfiguracja pozostaje dostępna, gdy nie ma zapisanych sieci
lub nie ukończono pierwszej konfiguracji.

## Codzienny ekran

Dolne przyciski przełączają na poprzednią stację, otwierają siatkę stacji i
przechodzą do następnej wspieranej stacji. Przycisk ustawień otwiera preferencje.

Dostępne są dwa układy ekranu głównego:

- **Pełny** eksponuje wygląd stacji i aktualny tytuł ICY, jeśli jest dostępny.
- **Minimalny** eksponuje zegar i celowo ogranicza informacje o stacji.

Wybór zapisuje się na urządzeniu. Jeśli stacja nie podaje tytułu utworu,
odpowiednie pole pokazuje tekst zastępczy albo znika.

## Ustawienia

Ustawienia są podzielone na trzy czytelnie ponumerowane strony:

- **Szybkie:** status Wi-Fi, Bluetooth, głośność, jasność, motyw i język.
- **System:** Ekran główny, Wi-Fi i sieci, O projekcie, Diagnostyka, Wyczyść dane i Konsola.
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
10 minut. Bez działającego czujnika ekran może wyłączyć się po 15, 30 albo 60 minutach. Gdy czujnik przekazuje odczyty, ekran pozostaje włączony, a wygaszacz nie uruchamia się automatycznie. Po odłączeniu czujnika ponownie obowiązują zapisane ustawienia. Odtwarzanie i przywracanie połączeń działają także przy wyłączonym ekranie. Dotyk budzi ekran i wraca do wybranego układu głównego.

## Zmiana Wi-Fi i dostęp do konsoli (zmienione w 0.4.2)

Sieć Wi-Fi zmienia się z własnego punktu dostępowego radia, a nie przez sieć
domową. W 0.4.5 punkt konfiguracji otworzysz przyciskiem **Dodaj sieć Wi-Fi**, również bez połączenia z domową siecią. Jeśli trwa sesja konsoli, najpierw wybierz **Zamknij konsolę**. Po ukończeniu konfiguracji radio z zapisaną siecią nie otwiera tego punktu automatycznie przy utracie Wi-Fi.

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

0.4.5 odebrano 28.10.2026 z wyjątkami, na jednej kostce Core2 v1.3 i jednym
głośniku Bluetooth.

- **Radio może się zrestartować, gdy ponownie łączy się z głośnikiem
  Bluetooth po zerwaniu łącza przez słaby sygnał.** W 21-godzinnym teście
  zdarzyło się to dwa razy; za każdym razem radio samo wróciło z Wi-Fi,
  stacją i głośnikiem w ciągu około dwóch minut. Wyłączenie i włączenie
  głośnika tego nie wywołuje. Wydanie 0.4.4 ma ten sam kod ponownego
  łączenia. Poprawka jest planowana w 0.4.6.
- **Zdarzenie ochrony Wi-Fi nadal może przerwać odtwarzanie na około minutę.**
  W sieci mieszanej CCMP/TKIP router zablokował Wi-Fi z powodem 14 (błąd MIC)
  jedenaście razy w ciągu jednej nocy na obrazie wydania; za każdym razem
  dźwięk znikał na około minutę i wracał bez pomocy.
- **Głośnik kostki może przerywać co kilka sekund**, także gdy dane stacji
  przychodzą na czas. Nie sprawdziliśmy, czy 0.4.4 zachowuje się tak samo.
- **Drugiego głośnika Bluetooth nie zakwalifikowano dla tego wydania.**
  Z jedynym testowanym głośnikiem osiem niezakłóconych cykli zasilania
  wróciło w 3,2–16,5 sekundy. W jednym cyklu, gdy obok leżał telefon
  sparowany z tym samym głośnikiem, powrót trwał 72 sekundy.
- Adres stacji, który nie odpowiada, może zatrzymać reakcję ekranu na kilka
  sekund podczas otwierania połączenia.
- Dźwięk Bluetooth może czekać na dostateczną ilość pamięci. Głośnik może też
  wymagać kolejnej próby połączenia; wcześniejszego ponownego otwarcia
  strumienia przy łączeniu głośnika nie uznano jeszcze za rozwiązane w 0.4.5.
- Opóźnione metadane ICY nadal mogą powodować ponowne połączenie strumienia.
  0.4.5 nie dodaje szczegółowych objaśnień tych przerw na ekranie; odłożono je
  do 0.4.6. Nie ma komunikatów głosowych.
- Obrazu fabrycznego 0.4.5 i konfiguracji z wyczyszczonej pamięci, aktualizacji
  ze strony instalacji, zmiany stacji ani parowania nowego głośnika nie
  sprawdzono na bajtach wydania. Testy innych rewizji Core2, pełnego katalogu,
  czasu na baterii i opcjonalnych czujników również są przed nami.
- Każda z dziewięciu wybranych stacji używa ścieżki MP3 z katalogu 236 stacji.
  Dostępność adresów może się zmieniać niezależnie od wersji firmware.
  AAC, HLS i głośniki wyłącznie Bluetooth LE Audio nie są obsługiwane.
- Oficjalne logotypy stacji nie są publicznie dołączane bez zgody.
