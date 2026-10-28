# Współpraca

[English version](CONTRIBUTING.md)

**Opublikowanym wydaniem jest 0.4.5.** Oddzielaj obserwacje z obrazów
diagnostycznych od wyników na obrazach wydania.

Projekt wydaje publiczny firmware (0.1, 0.2, 0.2.1, 0.4, 0.4.1, 0.4.2, 0.4.4, aktualnie 0.4.5) zwalidowany na realnym sprzęcie.
Wkład jest przyjmowany jako:

- software na GPL-3.0-or-later,
- oryginalna dokumentacja/grafiki na CC BY-SA 4.0,
- dane katalogowe na CC0-1.0.

Wnosząc wkład, potwierdzasz prawo do przekazania materiału na właściwej
licencji projektu.

## Licencja na wkład

Wniesienie wkładu udziela maintenerowi także nieodpłatnej, niewyłącznej licencji
z prawem udzielania sublicencji, również na warunkach innych niż
GPL-3.0-or-later. **Prawa autorskie zostają przy Tobie**; nic, co już wyszło,
nie przestaje być na GPL. Powód jest prosty: projektu z jednym maintenerem nie
może po latach zablokować jedna zmiana, której autora nie da się odnaleźć.

Pełne warunki, pola eksploatacji i możliwość odmowy są w
[CONTRIBUTOR-LICENCE.pl.md](CONTRIBUTOR-LICENCE.pl.md). Przeczytaj przed
pierwszym pull requestem — jest krótki i jako jedyny dokument tutaj ma skutek
prawny.

## Zasady

- Stabilny rdzeń pozostaje mały.
- Kod, identyfikatory i interfejsy techniczne są po angielsku.
- Publiczna dokumentacja użytkownika zachowuje parytet EN/PL.
- Nie dodawaj telemetrii, zależności od usług chmurowych, automatycznych kanałów aktualizacji ani zdalnego wykonywania kodu.
- Bez oficjalnych logotypów stacji bez manifestu praw i udokumentowanej zgody.
- Oryginalne motywy stacji są mile widziane; prywatne assety trafiają do ignorowanego `assets-local/`.
- Bez nagrań, endpointów premium i nieoficjalnych proxy streamów.
- Każdy endpoint stacji ma oficjalne źródło i datę weryfikacji.

## Pull request

1. Większa zmiana ma issue albo RFC.
2. Jeden PR rozwiązuje jeden problem.
3. Dołącz testy oraz wyniki sprawdzenia na urządzeniu, jeśli zmiana tego wymaga.
4. Instrukcje EN/PL są aktualizowane razem.
5. Commit zawiera DCO: `Signed-off-by: Imię <email>`.

Sieć, security, USB rescue i katalog `stable` wymagają dwóch review. Pozostałe zmiany
wymagają jednego review maintenera. Każdy wkład przechodzi przegląd
i walidację maintenera przed scaleniem.

## Dodawanie stacji

Podaj operatora, oficjalny player, źródło resolvera, kodek, transport, region,
datę weryfikacji i wynik testu na Core2. `Verified` oznacza test techniczny, a
nie akceptację lub rekomendację nadawcy.

## Dowody społecznościowe

Dowody zgodności przysyłaj jako ustrukturyzowany JSON. Raport nie może zawierać
URL-i endpointów, SSID, danych logowania, adresów Bluetooth, numerów urządzeń,
tożsamości słuchacza, PCM ani grafik. Każdy raport waliduje maintener, zanim
zostanie użyty. Wynik `PASS` jest dowodem software dla wskazanego hasha
firmware, a nie ogólną deklaracją zgodności sprzętowej.

Procedurę budowania i sprawdzania zgodności z wydaniem opisuje `REPRODUCIBILITY.pl.md`.
