# Powtarzalność

[English version](REPRODUCIBILITY.md)

Archiwa źródłowe są powtarzalne bitowo, a wydany firmware odtwarza się
bajt-w-bajt poza stemplem pochodzenia przy zachowaniu trzech parametrów builda.
Zmierzone na 0.2.1 (2026-07-23) i **udowodnione ponownie wykonaniem na 0.4**
(2026-07-31): opublikowany obraz odbudowano z commitu późniejszego niż własne
cięcie kodu, stemplując zapisane cięcie, i hash się zgodził.

1. **Piny zależności, po nazwie.** Każda biblioteka z
   `firmware/manifests/dependencies.lock.json` musi rozwiązać się dokładnie
   raz; M5GFX jest przypięty wpisem *nazwanym*
   (`M5GFX=https://github.com/m5stack/M5GFX.git#ad9b814…`, zawartość
   identyczna z rejestrowym 0.2.25).
2. **Ścieżka katalogu builda — zneutralizowana w 0.4.1.** Dwa stringi assertów
   frameworka osadzały absolutną ścieżkę katalogu, więc pełna zgodność bajtowa
   wymagała tej samej ścieżki builda, a opublikowana binarka niosła katalog
   domowy i nazwę użytkownika maszyny budującej. Skrypt pre-build podaje teraz
   `-ffile-prefix-map` liczony z drzewa, które akurat buduje, więc te stringi
   czyta się jako `/open-radio/...` i bajty nie zależą już od miejsca
   rozpakowania źródeł. Cztery ścieżki nazywające maszynę, która zbudowała
   *toolchain*, zostają w gotowych bibliotekach nieprzebudowywanych przez ten
   projekt; są identyczne dla każdego, kto używa przypiętego toolchainu, więc
   nie wpływają na odtworzenie.
3. **Stempel commita gita.** Build osadza `git rev-parse HEAD`; budowa źródeł
   0.2.1 spod innego commita różni się dokładnie 104 bajtami pól pochodzenia
   (hash ELF w deskryptorze aplikacji, 40-znakowy identyfikator commita i
   końcowy hash obrazu). Po wyłączeniu tych pól obrazy są identyczne bajtowo;
   budowa na samym tagu wydania daje pełną równość.

## Instalacja toolchainu

```bash
scripts/bootstrap-firmware-toolchain.sh
```

Skrypt tworzy odizolowane środowisko Pythona według
`firmware/manifests/requirements-firmware.lock.txt`, instaluje w nim przypięte
PlatformIO, CMake i Ninja, a na koniec rozwiązuje biblioteki dla
`firmware/public-candidate`. `pio` z poniższego polecenia to właśnie to
zainstalowane, `.tools/venv/bin/pio`.

## Odtworzenie opublikowanego wydania

Binarka niesie w sobie commit, z którego została wycięta, więc rekord nazywający
ten commit nie może w niej siedzieć: zapisanie rekordu przesuwa `HEAD`. Zamiast
kazać weryfikującemu zgadywać, który commit wyciągnąć, build stempluje zapisane
cięcie wprost.

```bash
git checkout open-radio-0-4      # tag niesie rekord
OPEN_RADIO_BUILD_SHA=<commit cięcia kodu> \
  pio run -d firmware/public-candidate -e core2-public-candidate
```

`OPEN_RADIO_BUILD_SHA` to cięcie kodu, z którego wzięto wydanie — commit, który
opublikowany obraz podaje jako własny. Zbuduj dwa razy, wymagaj bajtowej
identyczności obu przebiegów, a potem wymagaj, żeby hash równał się temu
opublikowanemu dla tego wydania. To, czy masz właściwe źródła, udowadnia ten
hash — obejmujący każdy plik — a nie `HEAD`, który nie obejmuje żadnego. Buduj
z czystego drzewa: ostemplowana binarka zbudowana z niezacommitowanych zmian
niesie deklarację pochodzenia, której nikt nie może sprawdzić.

**Tag wskazuje commit niosący rekord, nie samo cięcie kodu.** Oba drzewa różnią
się wyłącznie plikami, które nigdy nie są kompilowane, więc dają te same bajty;
otagowanie rekordu sprawia, że wystarczy jedno `git checkout`.
