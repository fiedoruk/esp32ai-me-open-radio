#pragma once

#include <cstddef>

#include "open_radio/service_contracts.hpp"

namespace open_radio {
namespace generated {

static const char k_index_html[] = R"OPEN_RADIO_ASSET(<!doctype html>
<html lang="pl">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
  <meta name="color-scheme" content="dark">
  <meta name="theme-color" content="#0c1014">
  <meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
  <link rel="icon" href="data:,">
  <title>Konfiguracja Wi-Fi radia</title>
  <link rel="stylesheet" href="/network-onboarding/styles.css">
</head>
<body>
  <svg class="sprite" aria-hidden="true" focusable="false"><defs>
    <symbol id="tabler-wifi" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 18l.01 0"/><path d="M9.172 15.172a4 4 0 0 1 5.656 0"/><path d="M6.343 12.343a8 8 0 0 1 11.314 0"/><path d="M3.515 9.515c4.686 -4.687 12.284 -4.687 17 0"/></symbol>
    <symbol id="tabler-bluetooth" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M7 8l10 8l-5 4l0 -16l5 4l-10 8"/></symbol>
    <symbol id="tabler-check" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M5 12l5 5l10 -10"/></symbol>
    <symbol id="tabler-alert-triangle" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 9v4"/><path d="M10.363 3.591l-8.106 13.534a1.914 1.914 0 0 0 1.636 2.871h16.214a1.914 1.914 0 0 0 1.636 -2.87l-8.106 -13.536a1.914 1.914 0 0 0 -3.274 0"/><path d="M12 16h.01"/></symbol>
    <symbol id="tabler-chevron-left" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M15 6l-6 6l6 6"/></symbol>
    <symbol id="tabler-radio" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M14 3l-9.371 3.749a1 1 0 0 0 -.629 .928v11.323a1 1 0 0 0 1 1h14a1 1 0 0 0 1 -1v-11a1 1 0 0 0 -1 -1h-14.5"/><path d="M4 12h16"/><path d="M7 12v-2"/><path d="M17 16v.01"/><path d="M13 16v.01"/></symbol>
    <symbol id="tabler-shield-check" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M11.46 20.846a12 12 0 0 1 -7.96 -14.846a12 12 0 0 0 8.5 -3a12 12 0 0 0 8.5 3a12 12 0 0 1 -.09 7.06"/><path d="M15 19l2 2l4 -4"/></symbol>
    <symbol id="tabler-refresh" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M20 11a8.1 8.1 0 0 0 -15.5 -2m-.5 -4v4h4"/><path d="M4 13a8.1 8.1 0 0 0 15.5 2m.5 4v-4h-4"/></symbol>
    </defs></svg>
  <template id="signal-icon"><svg aria-hidden="true"><use href="#tabler-wifi"/></svg></template>
  <main class="shell" aria-labelledby="page-title">
    <header class="topbar">
      <div>
        <p class="eyebrow" data-copy="localOnly">Lokalna konfiguracja urządzenia</p>
        <h1 id="page-title" data-copy="title">Połącz radio</h1>
      </div>
      <div class="language" role="group" aria-label="Language">
        <button type="button" data-locale="pl" aria-pressed="true">PL</button>
        <button type="button" data-locale="en" aria-pressed="false">EN</button>
      </div>
    </header>

    <section class="privacy-strip" aria-label="Privacy">
      <svg class="glyph" aria-hidden="true"><use href="#tabler-shield-check"/></svg><strong data-copy="privacyTitle">Bez konta i analityki.</strong>
      <span data-copy="privacyBody">Hasło zostaje w kostce. Radio nie sprawdza lokalizacji ani pogody; godzinę pobiera z internetu, strefa czasowa jest polska.</span>
    </section>

    <section class="panel" id="choice-panel">
      <div class="step"><span>1</span><div><h2 data-copy="choiceTitle">Jakie stacje ma grać?</h2><p data-copy="choiceBody">Możesz wziąć gotową dziewiątkę i skończyć w jedną sekundę albo wybrać własne stacje z listy sprawdzonych.</p></div></div>
      <div class="actions two">
        <button class="primary" type="button" id="choice-default" data-copy="choiceDefault">Weź gotową dziewiątkę</button>
        <button type="button" id="choice-custom" data-copy="choiceCustom">Wybiorę sam</button>
      </div>
      <ol class="default-nine" id="default-nine"></ol>
    </section>

    <section class="panel" id="stations-panel" hidden>
      <button class="back" type="button" id="stations-back" data-copy="back"><svg class="glyph" aria-hidden="true"><use href="#tabler-chevron-left"/></svg>Wstecz</button>
      <div class="step"><span>1</span><div><h2 data-copy="stationsTitle">Wybierz dziewięć stacji</h2><p id="stations-progress"></p></div></div>
      <label for="station-search" class="sr-only" data-copy="stationsSearch">Szukaj stacji</label>
      <input id="station-search" type="search" autocomplete="off" autocapitalize="none" autocorrect="off" spellcheck="false" enterkeyhint="search" placeholder="Szukaj…">
      <div class="network-list" id="station-list"></div>
      <p id="station-list-note" class="list-note" hidden></p>
      <div class="slot-strip" id="slot-strip"></div>
      <button class="primary" type="button" id="stations-done" data-copy="stationsDone">Gotowe, dalej do Wi-Fi</button>
    </section>

    <section class="panel" id="networks-panel" hidden>
      <div class="step"><span>1</span><div><h2 data-copy="networksTitle">Wybierz sieć 2.4 GHz</h2><p data-copy="networksBody">Automatycznie używamy później tylko zatwierdzonych, zabezpieczonych profili.</p></div></div>
      <div class="network-list" id="network-list"></div>
      <button class="primary" type="button" id="manual-network" data-copy="manualNetwork">Dodaj hotspot, którego jeszcze nie widać</button>
    </section>

    <section class="panel" id="credentials-panel" hidden>
      <button class="back" type="button" id="credentials-back" data-copy="back"><svg class="glyph" aria-hidden="true"><use href="#tabler-chevron-left"/></svg>Wstecz</button>
      <div class="step"><span>1</span><div><h2 data-copy="passwordTitle">Hasło do sieci</h2><p id="selected-network-label"></p></div></div>
      <form id="credentials-form" novalidate>
        <div id="manual-ssid-group" hidden>
          <label for="network-ssid" data-copy="ssidLabel">Nazwa hotspotu (SSID)</label>
          <input id="network-ssid" type="text" minlength="1" maxlength="32" autocomplete="off" autocapitalize="none" autocorrect="off" spellcheck="false" enterkeyhint="next">
        </div>
        <label for="network-password" data-copy="passwordLabel">Hasło Wi-Fi</label>
        <div class="password-row">
          <input id="network-password" type="password" minlength="8" maxlength="63" autocomplete="off" autocapitalize="none" autocorrect="off" spellcheck="false" enterkeyhint="go" required>
          <button type="button" id="toggle-password" aria-pressed="false" data-copy="show">Pokaż</button>
        </div>
        <p class="error" id="credential-error" role="alert" hidden data-copy="passwordError">Wpisz od 8 do 63 znaków.</p>
        <button class="primary" type="submit" data-copy="connect">Połącz i uruchom radio</button>
      </form>
    </section>

    <section class="panel" id="waiting-hotspot-panel" hidden>
      <div class="status-icon ok" aria-hidden="true"><svg><use href="#tabler-check"/></svg></div>
      <h2 data-copy="hotspotReadyTitle">Dane przyjęte. Teraz włącz hotspot.</h2>
      <p data-copy="hotspotReadyBody">Możesz rozłączyć się z OpenRadio-Setup. Kostka przez 2 minuty szuka podanej, zabezpieczonej sieci i zapisze profil dopiero po udanym połączeniu.</p>
    </section>

    <section class="panel blocked" id="blocked-panel" hidden>
      <div class="status-icon" aria-hidden="true"><svg><use href="#tabler-alert-triangle"/></svg></div>
      <h2 id="blocked-title">Ta sieć wymaga ręcznej decyzji</h2>
      <p id="blocked-body"></p>
      <button class="primary" type="button" id="blocked-back" data-copy="chooseAnother">Wybierz inną sieć</button>
    </section>

    <section class="panel success" id="first-sound-panel" hidden>
      <div class="now-playing">
        <div class="pulse" aria-hidden="true"><svg><use href="#tabler-radio"/></svg></div>
        <div><p data-copy="playingNow">RADIO JUŻ GRA</p><h2>RMF FM</h2><span data-copy="localSpeaker">Głośnik radia · 42%</span></div>
      </div>
      <div class="step optional"><span>2</span><div><h2 data-copy="cityTitle"><svg class="glyph" aria-hidden="true"><use href="#tabler-refresh"/></svg>Zegar ustawia się sam</h2><p data-copy="cityBody">Po Wi-Fi radio pobiera godzinę z internetu i pracuje w polskiej strefie czasowej. Miasta ani pogody nie sprawdza.</p></div></div>
      <div class="actions two">
        <button class="primary" type="button" id="city-auto" data-copy="cityAuto">Dalej</button>
        <button type="button" id="city-skip" data-copy="skip">Pomiń</button>
      </div>
    </section>

    <section class="panel" id="bluetooth-panel" hidden>
      <div class="now-playing compact"><div class="pulse" aria-hidden="true"></div><span data-copy="keepsPlaying">Radio nadal gra lokalnie</span></div>
      <div class="step optional"><span>3</span><div><h2 data-copy="bluetoothTitle"><svg class="glyph" aria-hidden="true"><use href="#tabler-bluetooth"/></svg>Głośnik Bluetooth — opcjonalnie</h2><p data-copy="bluetoothBody">Parowanie nie blokuje radia. Możesz zrobić je później na kostce.</p></div></div>
      <div class="actions two">
        <button class="primary" type="button" id="bluetooth-later" data-copy="pairLater">Sparuj później</button>
        <button type="button" id="bluetooth-skip" data-copy="useLocal">Zostań przy głośniku radia</button>
      </div>
    </section>

    <section class="panel complete" id="console-saved-panel" hidden>
      <div class="status-icon ok" aria-hidden="true"><svg><use href="#tabler-check"/></svg></div>
      <h2 data-copy="consoleSavedTitle">Zapisano. Radio restartuje się z nowymi stacjami.</h2>
      <p data-copy="consoleSavedBody">Kostka pobierze logotypy wybranych stacji i wróci do grania. Możesz zamknąć tę kartę.</p>
    </section>

    <section class="panel complete" id="complete-panel" hidden>
      <div class="status-icon ok" aria-hidden="true"><svg><use href="#tabler-check"/></svg></div>
      <h2 data-copy="completeTitle">Gotowe. Następnym razem radio uruchomi się samo.</h2>
      <p data-copy="completeBody">Zapamiętany został wyłącznie lokalny profil urządzenia. Nie ma konta ani chmury.</p>
      <dl><div><dt data-copy="wifiStatus">Wi-Fi</dt><dd data-copy="connected">Połączono</dd></div><div><dt data-copy="audioStatus">Dźwięk</dt><dd data-copy="playing">Startuje</dd></div></dl>
    </section>

    <footer><span data-copy="footer">Open Radio Core2 · Tomasz Fiedoruk · open-radio.me</span><span id="stage-label">NETWORKS</span></footer>
  </main>
  <script type="module" src="/network-onboarding/app.mjs"></script>
</body>
</html>
)OPEN_RADIO_ASSET";

static const char k_styles_css[] = R"OPEN_RADIO_ASSET(:root {
  color: #f5f5ef;
  background: #0c1014;
  font-family: Inter, ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
  font-synthesis: none;
}

* { box-sizing: border-box; }
body { margin: 0; min-height: 100vh; min-height: 100dvh; background: radial-gradient(circle at 20% 0%, #213d38 0, transparent 36rem), #0c1014; }
/* The page is painted edge to edge via viewport-fit=cover, so every outer
   edge has to earn its inset back or content slides under the notch and the
   home indicator. */
button, input { font: inherit; }
button { min-height: 44px; border: 1px solid #52605f; border-radius: 14px; color: inherit; background: #172024; cursor: pointer; }
button:hover { border-color: #95e4bd; }
button:focus-visible, input:focus-visible { outline: 3px solid #f3c969; outline-offset: 2px; }
.shell { width: min(760px, calc(100% - 32px - env(safe-area-inset-left) - env(safe-area-inset-right))); margin: 0 auto; padding: calc(28px + env(safe-area-inset-top)) 0 calc(20px + env(safe-area-inset-bottom)); }
.topbar { display: flex; align-items: flex-start; justify-content: space-between; gap: 24px; }
.eyebrow { margin: 0 0 7px; color: #9ab2ad; font-size: .78rem; font-weight: 800; letter-spacing: .12em; text-transform: uppercase; }
h1 { margin: 0; font-size: clamp(2rem, 8vw, 3.8rem); line-height: .96; max-width: 10ch; }
h2 { margin: 0; font-size: 1.25rem; overflow-wrap: anywhere; }
#selected-network-label { overflow-wrap: anywhere; }
p { color: #bcc8c5; line-height: 1.55; }
.language { display: flex; gap: 6px; }
.language button { min-width: 48px; padding: 0 12px; }
.language button[aria-pressed="true"] { color: #0c1014; border-color: #95e4bd; background: #95e4bd; font-weight: 900; }
.privacy-strip { display: flex; gap: 8px; margin: 28px 0 14px; padding: 13px 16px; border: 1px solid #2b5047; border-radius: 15px; background: #11231f; color: #b9d9cf; }
.panel { padding: clamp(20px, 5vw, 34px); border: 1px solid #29353a; border-radius: 24px; background: rgba(18, 24, 29, .94); box-shadow: 0 24px 80px rgba(0, 0, 0, .28); }
.step { display: flex; gap: 16px; align-items: flex-start; }
.step > span { display: grid; flex: 0 0 38px; width: 38px; height: 38px; place-items: center; border-radius: 12px; color: #0c1014; background: #f3c969; font-weight: 900; }
.step p { margin: 6px 0 0; }
.network-list { display: grid; gap: 10px; margin-top: 24px; }
.list-note { margin: 10px 2px 0; font-size: 14px; opacity: 0.72; }
/* min-width:0 twice, deliberately: a grid item defaults to min-width:auto,
   which is min-content, so a row containing an unbreakable SSID like
   Osiedlowa-Siec-24GHz-Goscie-0042 refuses to shrink and scrolls the whole
   setup page sideways on a 320 px phone. */
.network { display: grid; grid-template-columns: minmax(0, 1fr) auto; align-items: center; gap: 12px; padding: 16px; text-align: left; min-width: 0; }
.network > span { min-width: 0; }
.network strong, .network small { display: block; }
.network strong { overflow-wrap: anywhere; }
.network small { margin-top: 4px; color: #99aaa8; }
.badge { padding: 6px 9px; border-radius: 999px; color: #c9d4d1; background: #263236; font-size: .74rem; font-weight: 800; white-space: nowrap; }
.badge.safe { color: #11251f; background: #95e4bd; }
.back { margin-bottom: 20px; padding: 0 14px; border: 0; background: transparent; }
form { margin-top: 24px; }
label { display: block; margin-bottom: 8px; font-weight: 800; }
.password-row { display: grid; grid-template-columns: 1fr auto; gap: 8px; }
input { width: 100%; min-height: 50px; padding: 0 14px; border: 1px solid #52605f; border-radius: 14px; color: #fff; background: #0d1316; }
.password-row button { padding: 0 16px; }
.primary { border-color: #95e4bd; color: #0c1714; background: #95e4bd; font-weight: 900; }
form > .primary { width: 100%; margin-top: 14px; }
.error { margin: 8px 0 0; color: #ffaaa0; }
.blocked, .complete { text-align: center; }
.status-icon { display: grid; width: 58px; height: 58px; margin: 0 auto 18px; place-items: center; border-radius: 18px; color: #241511; background: #f39a76; font-size: 2rem; font-weight: 900; }
.status-icon.ok { color: #0e201a; background: #95e4bd; }
.blocked .primary { padding: 0 24px; }
.now-playing { display: flex; align-items: center; gap: 18px; margin: -8px -8px 28px; padding: 18px; border-radius: 18px; background: linear-gradient(135deg, #5a1f32, #a73f39); }
.now-playing p, .now-playing h2, .now-playing span { margin: 0; color: #fff; }
.now-playing p { font-size: .74rem; font-weight: 900; letter-spacing: .12em; }
.now-playing h2 { margin: 3px 0; font-size: 1.8rem; }
.pulse { width: 38px; height: 38px; border: 9px solid rgba(255,255,255,.28); border-radius: 50%; background: #fff; box-shadow: 0 0 0 6px rgba(255,255,255,.12); }
.optional > span { background: #9cc7ed; }
.actions { margin-top: 24px; }
.actions.two { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
.now-playing.compact { margin-bottom: 26px; padding: 13px 16px; background: #253d48; }
.now-playing.compact .pulse { width: 22px; height: 22px; border-width: 5px; }
.complete dl { display: grid; gap: 8px; margin: 24px 0 0; }
.complete dl div { display: flex; justify-content: space-between; padding: 12px 14px; border-radius: 12px; background: #0d1417; }
.complete dd { margin: 0; color: #95e4bd; font-weight: 900; }
footer { display: flex; justify-content: space-between; gap: 20px; padding: 16px 4px 0; color: #74817f; font-size: .76rem; }

@media (max-width: 520px) {
  .shell { width: min(100% - 20px, 760px); padding-top: 18px; }
  .topbar { gap: 12px; }
  h1 { font-size: 2.25rem; }
  .privacy-strip { display: block; }
  .actions.two { grid-template-columns: 1fr; }
  footer { display: grid; }
}

@media (prefers-reduced-motion: no-preference) {
  .pulse { animation: pulse 1.7s ease-in-out infinite; }
  @keyframes pulse { 50% { transform: scale(.9); opacity: .72; } }
  /* A CSS animation restarts whenever an element goes from display:none to
     shown, which is exactly what toggling [hidden] does — so each step of the
     flow arrives instead of snapping into place. No JS, no transition on a
     discrete property, and it simply does not run when motion is reduced. */
  .panel:not([hidden]) { animation: panel-in .26s cubic-bezier(.2, .7, .3, 1) both; }
  @keyframes panel-in { from { opacity: 0; transform: translateY(10px); } }
  .network { transition: transform .12s ease, border-color .12s ease; }
  .network:active { transform: scale(.985); }
}

/* The cube and the phone are one product, so they draw from one icon set and
   one station identity. Every symbol below is copied verbatim from
   ui-contract/icons/tabler-core2.svg — the same sprite the device renders —
   and a test fails the build if the two ever drift apart. */
.sprite { position: absolute; width: 0; height: 0; overflow: hidden; }
.glyph { width: 1.15em; height: 1.15em; flex: none; vertical-align: -.18em; }
.status-icon svg { width: 30px; height: 30px; }
.back { display: inline-flex; align-items: center; gap: 8px; }
.privacy-strip { align-items: flex-start; }
.privacy-strip .glyph { margin-top: .15em; color: #95e4bd; }

/* Signal strength stops being three text dots and becomes the cube's wifi mark,
   dimmed by how much of it is real. */
.signal { display: inline-flex; align-items: center; gap: 6px; }
.signal svg { width: 16px; height: 16px; }
.signal[data-strength="weak"] svg { opacity: .4; }
.signal[data-strength="fair"] svg { opacity: .7; }

/* The monogram the installer taps here is the monogram that appears on the
   cube's tile: same letters, same accent, sourced from station-identities. */
.pulse { display: grid; place-items: center; }
.pulse svg { width: 18px; height: 18px; color: #a73f39; }
.now-playing.compact .pulse svg { width: 11px; height: 11px; }
.step h2 { display: flex; align-items: center; gap: 9px; }

.monogram { display: grid; flex: none; width: 46px; height: 46px; place-items: center; border-radius: 13px; font-size: .74rem; font-weight: 900; letter-spacing: .02em; }

/* Choosing the nine. The strip mirrors the cube's own 3x3 face, so the phone
   shows the thing the installer is holding rather than an abstract list. */
.sr-only { position: absolute; width: 1px; height: 1px; margin: -1px; padding: 0; overflow: hidden; clip-path: inset(50%); white-space: nowrap; }
.default-nine { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 8px; margin: 22px 0 0; padding: 0; list-style: none; counter-reset: none; }
.default-nine li { padding: 11px 8px; border: 1px solid #29353a; border-radius: 12px; background: #0f1519; color: #bcc8c5; font-size: .8rem; font-weight: 700; text-align: center; overflow-wrap: anywhere; }
#station-search { margin: 22px 0 14px; }
.slot-strip { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 8px; margin-bottom: 18px; }
.slot { min-height: 54px; padding: 8px 6px; border: 1px dashed #3c4a4f; border-radius: 12px; background: #0f1519; color: #8fa3a0; font-size: .74rem; font-weight: 800; text-align: center; overflow-wrap: anywhere; }
.slot.filled { border-style: solid; border-color: #95e4bd; color: #dff5ea; background: #12211d; }
.slot.builtin { border-style: solid; border-color: #3c4a4f; color: #bcc8c5; }
button.slot { cursor: pointer; font: inherit; }
.slot .index { display: block; margin-bottom: 3px; color: #6d807d; font-size: .66rem; }
#station-list { max-height: 46vh; overflow-y: auto; -webkit-overflow-scrolling: touch; }
.station-row { display: grid; grid-template-columns: minmax(0, 1fr) auto; align-items: center; gap: 12px; padding: 14px 16px; text-align: left; min-width: 0; }
.station-row > span { min-width: 0; }
.station-row strong { display: block; overflow-wrap: anywhere; }
.station-row small { display: block; margin-top: 3px; color: #99aaa8; }
.station-row.chosen { border-color: #95e4bd; background: #12211d; }
#stations-progress { margin: 6px 0 0; }
)OPEN_RADIO_ASSET";

static const char k_state_machine_mjs[] = R"OPEN_RADIO_ASSET(export const OnboardingStages = Object.freeze({
  // Stations come first and Wi-Fi last, and that order is forced rather than
  // chosen: in AP+STA the ESP32 moves its access point to the router's channel
  // the moment the station interface associates, dropping the phone. Anything
  // that needs the phone has to happen before the credentials are accepted.
  CHOICE: "CHOICE",
  STATIONS: "STATIONS",
  NETWORKS: "NETWORKS",
  CREDENTIALS: "CREDENTIALS",
  WAITING_HOTSPOT: "WAITING_HOTSPOT",
  BLOCKED: "BLOCKED",
  FIRST_SOUND: "FIRST_SOUND",
  BLUETOOTH: "BLUETOOTH",
  COMPLETE: "COMPLETE",
  // Console flow (page served over the home network): saving stations is the
  // whole errand, so it ends here instead of walking the provisioning steps.
  CONSOLE_SAVED: "CONSOLE_SAVED"
});

export function createOnboardingState(locale = "pl") {
  if (!new Set(["pl", "en"]).has(locale)) throw new Error("locale is invalid");
  return {locale, stage: OnboardingStages.CHOICE, selectedNetworkId: null, manualNetwork: false, blockReason: null, validationError: null, firstSoundStarted: false, cityMode: null, bluetoothMode: null, slots: null};
}

export function chooseDefaultNine(state) {
  if (state.stage !== OnboardingStages.CHOICE) throw new Error("station choice is not expected");
  return {...state, stage: OnboardingStages.NETWORKS, slots: null};
}

// -1 marks "the factory station stays in this slot". Choosing your own
// stations starts FROM the factory nine, not from nine holes: the realistic
// job is "swap the two I dislike", not "rebuild a radio from scratch".
export const BUILT_IN_STATION = -1;

export function chooseOwnStations(state) {
  if (state.stage !== OnboardingStages.CHOICE) throw new Error("station choice is not expected");
  return {...state, stage: OnboardingStages.STATIONS, slots: Array(9).fill(BUILT_IN_STATION)};
}

// Tapping a slot cycles it: factory -> free, custom -> free, free -> factory.
export function toggleSlot(state, position) {
  if (state.stage !== OnboardingStages.STATIONS) throw new Error("station choice is not expected");
  if (!Number.isInteger(position) || position < 0 || position > 8) throw new Error("slot position is invalid");
  const slots = [...state.slots];
  slots[position] = slots[position] === null ? BUILT_IN_STATION : null;
  return {...state, slots};
}

// A slot is filled left to right; tapping a station already chosen removes it.
// Nine is the whole product, so there is no "add more" and no scrolling list
// of favourites to manage — the constraint is the feature.
export function toggleStationChoice(state, directoryIndex) {
  if (state.stage !== OnboardingStages.STATIONS) throw new Error("station choice is not expected");
  if (!Number.isInteger(directoryIndex) || directoryIndex < 0) throw new Error("directory index is invalid");
  const slots = [...(state.slots ?? Array(9).fill(BUILT_IN_STATION))];
  const existing = slots.indexOf(directoryIndex);
  if (existing >= 0) {
    slots[existing] = BUILT_IN_STATION;
    return {...state, slots};
  }
  // A station goes into a freed slot first; with none freed, it replaces the
  // first factory slot — so "just add TOK FM" is one tap, no clearing ritual.
  let target = slots.indexOf(null);
  if (target < 0) target = slots.indexOf(BUILT_IN_STATION);
  if (target < 0) return state;
  slots[target] = directoryIndex;
  return {...state, slots};
}

export function confirmStationChoice(state) {
  if (state.stage !== OnboardingStages.STATIONS) throw new Error("station choice is not expected");
  return {...state, stage: OnboardingStages.NETWORKS};
}

export function restartStationChoice(state) {
  return {...state, stage: OnboardingStages.CHOICE, slots: null};
}

export function selectManualNetwork(state) {
  return {...state, stage: OnboardingStages.CREDENTIALS, selectedNetworkId: null, manualNetwork: true, blockReason: null, validationError: null};
}

export function selectOnboardingNetwork(state, network) {
  if (!network || typeof network.id !== "string") throw new Error("network is invalid");
  if (network.security === "OPEN") return {...state, stage: OnboardingStages.BLOCKED, selectedNetworkId: network.id, manualNetwork: false, blockReason: "OPEN", validationError: null};
  if (network.captivePortal) return {...state, stage: OnboardingStages.BLOCKED, selectedNetworkId: network.id, manualNetwork: false, blockReason: "CAPTIVE", validationError: null};
  if (!new Set(["WPA2_PSK", "WPA3_SAE"]).has(network.security)) return {...state, stage: OnboardingStages.BLOCKED, selectedNetworkId: network.id, manualNetwork: false, blockReason: "UNKNOWN_SECURITY", validationError: null};
  if (network.known === true) return {...state, stage: OnboardingStages.FIRST_SOUND, selectedNetworkId: network.id, manualNetwork: false, blockReason: null, validationError: null, firstSoundStarted: true, cityMode: "AUTO_APPROXIMATE"};
  return {...state, stage: OnboardingStages.CREDENTIALS, selectedNetworkId: network.id, manualNetwork: false, blockReason: null, validationError: null};
}

export function submitCredentialLength(state, credentialLength) {
  if (state.stage !== OnboardingStages.CREDENTIALS) throw new Error("credentials are not expected");
  if (!Number.isInteger(credentialLength) || credentialLength < 8 || credentialLength > 63) return {...state, validationError: "CREDENTIAL_LENGTH"};
  return state.manualNetwork
    ? {...state, stage: OnboardingStages.WAITING_HOTSPOT, validationError: null, firstSoundStarted: false, cityMode: null}
    : {...state, stage: OnboardingStages.FIRST_SOUND, validationError: null, firstSoundStarted: true, cityMode: "AUTO_APPROXIMATE"};
}

export function chooseCityMode(state, cityMode) {
  if (state.stage !== OnboardingStages.FIRST_SOUND) throw new Error("city choice is not expected");
  if (!new Set(["AUTO_APPROXIMATE", "DISABLED"]).has(cityMode)) throw new Error("city mode is invalid");
  return {...state, stage: OnboardingStages.BLUETOOTH, cityMode};
}

export function chooseBluetoothMode(state, bluetoothMode) {
  if (state.stage !== OnboardingStages.BLUETOOTH) throw new Error("Bluetooth choice is not expected");
  if (!new Set(["PAIR_LATER", "LOCAL_SPEAKER"]).has(bluetoothMode)) throw new Error("Bluetooth mode is invalid");
  return {...state, stage: OnboardingStages.COMPLETE, bluetoothMode};
}

export function restartNetworkSelection(state) {
  return {...state, stage: OnboardingStages.NETWORKS, selectedNetworkId: null, manualNetwork: false, blockReason: null, validationError: null};
}
)OPEN_RADIO_ASSET";

static const char k_app_mjs[] = R"OPEN_RADIO_ASSET(import {BUILT_IN_STATION, OnboardingStages, chooseBluetoothMode, chooseCityMode, chooseDefaultNine, chooseOwnStations, confirmStationChoice, createOnboardingState, restartNetworkSelection, restartStationChoice, selectManualNetwork, selectOnboardingNetwork, submitCredentialLength, toggleSlot, toggleStationChoice} from "./state-machine.mjs";

// Cloned rather than built: creating SVG needs createElementNS, whose namespace
// argument is a URL, and the firmware surface gate scans every embedded asset
// for URLs. Keeping the markup in a template keeps that gate strict.
const signalIconTemplate = document.querySelector("#signal-icon");

const fallbackNetworks = [
  {id: "known-home", label: {pl: "Domowa 2.4 GHz", en: "Home 2.4 GHz"}, security: "WPA2_PSK", captivePortal: false, known: true, rssi: -48},
  {id: "new-secured", label: {pl: "Nowa sieć zabezpieczona", en: "New secured network"}, security: "WPA3_SAE", captivePortal: false, known: false, rssi: -66},
  {id: "open-cafe", label: {pl: "Otwarta sieć", en: "Open network"}, security: "OPEN", captivePortal: false, known: false, rssi: -52},
  {id: "hotel-captive", label: {pl: "Sieć z portalem logowania", en: "Captive portal network"}, security: "WPA2_PSK", captivePortal: true, known: false, rssi: -78}
];
let networks = fallbackNetworks;
let deviceApi = false;
let csrfToken = "";
let consoleMode = false;

const copy = {
  pl: {
    localOnly: "Lokalna konfiguracja urządzenia", title: "Połącz radio", privacyTitle: "Bez konta i analityki.", privacyBody: "Hasło zostaje w kostce. Radio nie sprawdza lokalizacji ani pogody; godzinę pobiera z internetu, strefa czasowa jest polska.",
    networksTitle: "Wybierz sieć 2.4 GHz", networksBody: "Automatycznie używamy później tylko zatwierdzonych, zabezpieczonych profili.", known: "Zapamiętana", secured: "Zabezpieczona", open: "Otwarta", captive: "Portal", signal: "Sygnał", choiceTitle: "Jakie stacje ma grać?", choiceBody: "Możesz wziąć gotową dziewiątkę i skończyć w jedną sekundę albo wybrać własne stacje z listy sprawdzonych.", choiceDefault: "Weź gotową dziewiątkę", choiceCustom: "Wybiorę sam", stationsTitle: "Wybierz dziewięć stacji", stationsSearch: "Szukaj stacji", stationsDone: "Gotowe, dalej do Wi-Fi", stationsChosen: "wybrane", stationsFree: "wolne", stationsEmpty: "Nic nie pasuje do tej frazy.", stationsAlternates: "zapasowe kanały", stationsSingle: "jeden kanał", stationsCount: "stacji", stationsCheckedFloor: "sprawdzone nie wcześniej niż", stationsTruncated: "Pokazano pierwszych {shown} z {total}. Wpisz nazwę, żeby dosięgnąć reszty.", strong: "mocny", fair: "średni", weak: "słaby", manualNetwork: "Dodaj hotspot, którego jeszcze nie widać",
    back: "← Wstecz", passwordTitle: "Hasło do sieci", ssidLabel: "Nazwa hotspotu (SSID)", passwordLabel: "Hasło Wi-Fi", show: "Pokaż", hide: "Ukryj", passwordError: "Wpisz nazwę sieci i hasło od 8 do 63 znaków.", connect: "Zapisz i połącz",
    hotspotReadyTitle: "Dane przyjęte. Teraz włącz hotspot.", hotspotReadyBody: "Możesz rozłączyć się z OpenRadio-Setup. Kostka przez 2 minuty szuka podanej, zabezpieczonej sieci i zapisze profil dopiero po udanym połączeniu.",
    blockedOpenTitle: "Otwarta sieć nie będzie zapamiętana", blockedOpenBody: "Radio nie łączy się automatycznie z otwartymi sieciami. Wybierz zabezpieczony profil.", blockedCaptiveTitle: "Portal logowania wymaga ręcznej obsługi", blockedCaptiveBody: "Radio nie omija regulaminów ani logowania captive portal. Wybierz inną sieć.", blockedUnknownTitle: "Nieznany typ zabezpieczenia", blockedUnknownBody: "Ta sieć nie spełnia kontraktu automatycznego połączenia.", chooseAnother: "Wybierz inną sieć",
    playingNow: "RADIO JUŻ GRA", localSpeaker: "Głośnik radia · 42%", cityTitle: "Zegar ustawia się sam", cityBody: "Po Wi-Fi radio pobiera godzinę z internetu i pracuje w polskiej strefie czasowej. Miasta ani pogody nie sprawdza.", cityAuto: "Dalej", skip: "Pomiń",
    keepsPlaying: "Radio nadal gra lokalnie", bluetoothTitle: "Głośnik Bluetooth — opcjonalnie", bluetoothBody: "Parowanie nie blokuje radia. Możesz zrobić je później na urządzeniu.", pairLater: "Sparuj później", useLocal: "Zostań przy głośniku radia",
    consoleTitle: "Konsola stacji", stationsSave: "Zapisz stacje", consoleSavedTitle: "Zapisano. Radio restartuje się z nowymi stacjami.", consoleSavedBody: "Kostka pobierze logotypy wybranych stacji i wróci do grania. Możesz zamknąć tę kartę.",
    completeTitle: "Gotowe. Radio zaraz zagra.", completeBody: "Ta strona zaraz się rozłączy — kostka przełącza się na Twoją sieć. Zegar ustawi się sam z internetu. Głośnik Bluetooth sparujesz na kostce, kiedy zechcesz. Profil zostaje w urządzeniu; nie ma konta ani zewnętrznej analityki.", wifiStatus: "Wi-Fi", connected: "Połączono", audioStatus: "Dźwięk", playing: "Gra", footer: "Open Radio Core2 · Tomasz Fiedoruk · open-radio.me"
  },
  en: {
    localOnly: "Local device setup", title: "Connect the radio", privacyTitle: "No account or analytics.", privacyBody: "The password stays on the device. The radio does not look up your location or the weather; it takes the time from the internet and runs on Polish time.",
    networksTitle: "Choose a 2.4 GHz network", networksBody: "Only approved secured profiles may be reused automatically.", known: "Remembered", secured: "Secured", open: "Open", captive: "Portal", signal: "Signal", choiceTitle: "Which stations should it play?", choiceBody: "Take the ready-made nine and be done in a second, or pick your own from the verified list.", choiceDefault: "Take the ready-made nine", choiceCustom: "I will choose", stationsTitle: "Choose nine stations", stationsSearch: "Search stations", stationsDone: "Done, on to Wi-Fi", stationsChosen: "chosen", stationsFree: "free", stationsEmpty: "Nothing matches that.", stationsAlternates: "backup edges", stationsSingle: "one edge", stationsCount: "stations", stationsCheckedFloor: "verified no earlier than", stationsTruncated: "Showing the first {shown} of {total}. Type a name to reach the rest.", strong: "strong", fair: "fair", weak: "weak", manualNetwork: "Add a hotspot that is not visible yet",
    back: "← Back", passwordTitle: "Network password", ssidLabel: "Hotspot name (SSID)", passwordLabel: "Wi-Fi password", show: "Show", hide: "Hide", passwordError: "Enter a network name and a password between 8 and 63 characters.", connect: "Save and connect",
    hotspotReadyTitle: "Credentials accepted. Turn on the hotspot now.", hotspotReadyBody: "You can disconnect from OpenRadio-Setup. The cube searches for the secured network for 2 minutes and saves the profile only after a successful connection.",
    blockedOpenTitle: "An open network will not be remembered", blockedOpenBody: "The radio never autojoins open networks. Choose a secured profile.", blockedCaptiveTitle: "A login portal needs manual handling", blockedCaptiveBody: "The radio does not bypass captive-portal terms or login. Choose another network.", blockedUnknownTitle: "Unknown security type", blockedUnknownBody: "This network does not satisfy the automatic connection contract.", chooseAnother: "Choose another network",
    playingNow: "RADIO IS ALREADY PLAYING", localSpeaker: "Radio speaker · 42%", cityTitle: "The clock sets itself", cityBody: "After Wi-Fi the radio takes the time from the internet and runs on Polish time. It does not look up your city or the weather.", cityAuto: "Continue", skip: "Skip",
    keepsPlaying: "Radio keeps playing locally", bluetoothTitle: "Bluetooth speaker — optional", bluetoothBody: "Pairing never blocks radio. You can do it later on the device.", pairLater: "Pair later", useLocal: "Keep radio speaker",
    consoleTitle: "Station console", stationsSave: "Save stations", consoleSavedTitle: "Saved. The radio restarts with the new stations.", consoleSavedBody: "The cube fetches the new stations' logos and returns to playing. You can close this tab.",
    completeTitle: "Done. The radio is about to play.", completeBody: "This page is about to disconnect — the cube is moving to your network. The clock sets itself from the internet. Pair a Bluetooth speaker on the cube whenever you like. The profile stays on the device; there is no account or outbound analytics.", wifiStatus: "Wi-Fi", connected: "Connected", audioStatus: "Audio", playing: "Playing", footer: "Open Radio Core2 · Tomasz Fiedoruk · open-radio.me"
  }
};

let state = createOnboardingState("pl");
const panels = new Map([
  [OnboardingStages.CHOICE, document.querySelector("#choice-panel")],
  [OnboardingStages.STATIONS, document.querySelector("#stations-panel")],
  [OnboardingStages.NETWORKS, document.querySelector("#networks-panel")],
  [OnboardingStages.CREDENTIALS, document.querySelector("#credentials-panel")],
  [OnboardingStages.WAITING_HOTSPOT, document.querySelector("#waiting-hotspot-panel")],
  [OnboardingStages.BLOCKED, document.querySelector("#blocked-panel")],
  [OnboardingStages.FIRST_SOUND, document.querySelector("#first-sound-panel")],
  [OnboardingStages.BLUETOOTH, document.querySelector("#bluetooth-panel")],
  [OnboardingStages.COMPLETE, document.querySelector("#complete-panel")],
  [OnboardingStages.CONSOLE_SAVED, document.querySelector("#console-saved-panel")]
]);
const passwordInput = document.querySelector("#network-password");
const ssidInput = document.querySelector("#network-ssid");

function translate() {
  document.documentElement.lang = state.locale;
  document.querySelectorAll("[data-copy]").forEach(element => {
    const value = copy[state.locale][element.dataset.copy];
    if (value) element.textContent = value;
  });
  document.querySelectorAll("[data-locale]").forEach(button => button.setAttribute("aria-pressed", String(button.dataset.locale === state.locale)));
}

let directory = [];
let directoryProbedAt = null;
const defaultNineNames = ["RMF FM", "Radio ZET", "TOK FM", "Antyradio", "Radio ESKA",
                          "RMF MAXX", "Radio Plus", "Złote Przeboje", "Meloradio"];

async function loadDirectory() {
  if (directory.length > 0) return;
  try {
    const response = await fetch("/api/directory", {cache: "no-store"});
    if (!response.ok) return;
    const token = response.headers.get("X-Open-Radio-CSRF");
    if (token) csrfToken = token;
    const payload = await response.json();
    if (!Array.isArray(payload.stations)) return;
    directory = payload.stations;
    directoryProbedAt = payload.probedAt ?? null;
    deviceApi = true;
  } catch {
    // No device: the picker simply has nothing to offer, and the choice screen
    // still lets the installer take the built-in nine.
    directory = [];
  }
}

function renderDefaultNine() {
  const list = document.querySelector("#default-nine");
  list.replaceChildren(...defaultNineNames.map(name => {
    const item = document.createElement("li");
    item.textContent = name;
    return item;
  }));
}

function renderSlotStrip() {
  const strip = document.querySelector("#slot-strip");
  const slots = state.slots ?? Array(9).fill(BUILT_IN_STATION);
  strip.replaceChildren(...slots.map((value, position) => {
    const cell = document.createElement("button");
    cell.type = "button";
    cell.className = value === null ? "slot" : value === BUILT_IN_STATION ? "slot builtin" : "slot filled";
    const index = document.createElement("span");
    index.className = "index";
    index.textContent = String(position + 1);
    const label = document.createElement("strong");
    if (value === BUILT_IN_STATION) label.textContent = defaultNineNames[position];
    else if (value === null) label.textContent = "—";
    else {
      const entry = directory.find(station => station.i === value);
      label.textContent = entry ? entry.n : "—";
    }
    cell.replaceChildren(index, label);
    cell.addEventListener("click", () => {
      state = toggleSlot(state, position);
      renderSlotStrip();
      renderStationList();
    });
    return cell;
  }));
}

// Eighty rows is what a phone can scroll without the list becoming a wall.
// The cap stays; what changes is that the installer is told about it.
const STATION_LIST_LIMIT = 80;

function renderStationList() {
  const list = document.querySelector("#station-list");
  const query = document.querySelector("#station-search").value.trim().toLowerCase();
  const slots = state.slots ?? [];
  const matches = directory.filter(station => query.length === 0 || station.n.toLowerCase().includes(query));
  const words = copy[state.locale];
  document.querySelector("#stations-progress").textContent =
    `${slots.filter(value => value !== null && value !== BUILT_IN_STATION).length}/9 ${words.stationsChosen}` +
    ` · ${directory.length} ${words.stationsCount}` +
    (directoryProbedAt ? ` · ${words.stationsCheckedFloor} ${directoryProbedAt}` : "");
  // The list is capped, and until now it was capped silently: an installer saw
  // eighty of two hundred and had no way to know the rest existed, or that
  // typing would reach them. A cap nobody is told about is a list that lies.
  const note = document.querySelector("#station-list-note");
  const shown = Math.min(matches.length, STATION_LIST_LIMIT);
  note.hidden = matches.length <= STATION_LIST_LIMIT;
  note.textContent = note.hidden ? "" : words.stationsTruncated
    .replace("{shown}", String(shown)).replace("{total}", String(matches.length));
  if (matches.length === 0) {
    const empty = document.createElement("p");
    empty.textContent = words.stationsEmpty;
    list.replaceChildren(empty);
    return;
  }
  list.replaceChildren(...matches.slice(0, STATION_LIST_LIMIT).map(station => {
    const button = document.createElement("button");
    button.type = "button";
    button.className = slots.includes(station.i) ? "network station-row chosen" : "network station-row";
    const identity = document.createElement("span");
    const label = document.createElement("strong");
    label.textContent = station.n;
    const detail = document.createElement("small");
    const logoMark = station.l === 1 ? " · logo" : "";
    detail.textContent = (station.e > 1 ? `${station.e} ${words.stationsAlternates}` : words.stationsSingle) + logoMark;
    identity.replaceChildren(label, detail);
    const badge = document.createElement("span");
    badge.className = slots.includes(station.i) ? "badge safe" : "badge";
    badge.textContent = slots.includes(station.i) ? String(slots.indexOf(station.i) + 1) : "+";
    button.replaceChildren(identity, badge);
    button.addEventListener("click", () => {
      state = toggleStationChoice(state, station.i);
      renderSlotStrip();
      renderStationList();
    });
    return button;
  }));
}

// Sent before Wi-Fi, while the phone can still reach the device at all.
async function postSlots(slots) {
  if (!deviceApi) return true;
  try {
    const response = await fetch("/api/slots", {
      method: "POST",
      headers: {"content-type": "application/json", "X-Open-Radio-CSRF": csrfToken},
      body: JSON.stringify((slots ?? Array(9).fill(null)).map(value => value === null ? -1 : value))
    });
    const result = await response.json();
    return response.ok && result.accepted === true;
  } catch {
    return false;
  }
}

function renderNetworks() {
  const list = document.querySelector("#network-list");
  list.replaceChildren(...networks.map(network => {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "network";
    button.dataset.networkId = network.id;
    const type = network.captivePortal ? copy[state.locale].captive : network.security === "OPEN" ? copy[state.locale].open : network.known ? copy[state.locale].known : copy[state.locale].secured;
    const safe = network.security !== "OPEN" && !network.captivePortal;
    const identity = document.createElement("span");
    const label = document.createElement("strong");
    const signal = document.createElement("small");
    const badge = document.createElement("span");
    label.textContent = network.label[state.locale];
    // The cube draws its own wifi mark for this; the phone should not invent a
    // second vocabulary out of text dots for the same fact.
    const strength = network.rssi >= -55 ? "strong" : network.rssi >= -72 ? "fair" : "weak";
    signal.className = "signal";
    signal.dataset.strength = strength;
    const signalIcon = signalIconTemplate.content.firstElementChild.cloneNode(true);
    const signalText = document.createElement("span");
    signalText.textContent = `${copy[state.locale].signal}: ${copy[state.locale][strength]}`;
    signal.replaceChildren(signalIcon, signalText);
    badge.className = `badge ${safe ? "safe" : ""}`;
    badge.textContent = type;
    identity.replaceChildren(label, signal);
    button.replaceChildren(identity, badge);
    button.addEventListener("click", () => {
      state = selectOnboardingNetwork(state, network);
      render();
    });
    return button;
  }));
}

function renderBlocked() {
  const title = document.querySelector("#blocked-title");
  const body = document.querySelector("#blocked-body");
  const prefix = state.blockReason === "OPEN" ? "blockedOpen" : state.blockReason === "CAPTIVE" ? "blockedCaptive" : "blockedUnknown";
  title.textContent = copy[state.locale][`${prefix}Title`];
  body.textContent = copy[state.locale][`${prefix}Body`];
}

let renderedStage = null;

function render() {
  translate();
  renderNetworks();
  const stageChanged = state.stage !== renderedStage;
  renderedStage = state.stage;
  for (const [stage, panel] of panels) panel.hidden = stage !== state.stage;
  document.querySelector("#stage-label").textContent = state.stage;
  document.querySelector("#credential-error").hidden = state.validationError === null;
  const selected = networks.find(network => network.id === state.selectedNetworkId);
  document.querySelector("#selected-network-label").textContent = state.manualNetwork ? copy[state.locale].manualNetwork : selected ? selected.label[state.locale] : "";
  document.querySelector("#manual-ssid-group").hidden = !state.manualNetwork;
  if (consoleMode) {
    // The console is a station errand, not provisioning: rename the header
    // and the confirm button so nothing promises a Wi-Fi step.
    const words = copy[state.locale];
    document.querySelectorAll("[data-copy=\"title\"]").forEach(element => { element.textContent = words.consoleTitle; });
    document.querySelector("#stations-done").textContent = words.stationsSave;
  }
  if (state.stage === OnboardingStages.CHOICE) renderDefaultNine();
  if (state.stage === OnboardingStages.STATIONS) { renderSlotStrip(); renderStationList(); }
  if (state.stage === OnboardingStages.BLOCKED) renderBlocked();
  // Only on arrival, never on a re-render: a password field that re-grabs focus
  // while someone is typing, or that reopens the keyboard after they dismissed
  // it, is worse than no autofocus at all. Arriving here means the installer
  // just tapped a network and the very next thing they must do is type.
  if (stageChanged && state.stage === OnboardingStages.CREDENTIALS) {
    (state.manualNetwork ? ssidInput : passwordInput).focus({preventScroll: false});
  }
}

document.querySelectorAll("[data-locale]").forEach(button => button.addEventListener("click", () => {
  state = {...state, locale: button.dataset.locale};
  render();
}));
document.querySelector("#choice-default").addEventListener("click", async () => {
  if (consoleMode) {
    // Console: the save IS the errand — the device closes the session and
    // restarts itself, so the confirmation must wait for the device.
    await postSlots(null);
    state = {...state, stage: OnboardingStages.CONSOLE_SAVED};
  } else {
    // Onboarding: the factory nine IS the fresh device's state, so there is
    // nothing to wait for — awaiting the POST over the busy AP link held the
    // installer 3-4 visible seconds before the Wi-Fi step.
    postSlots(null).catch(() => {});
    state = chooseDefaultNine(state);
  }
  render();
});
document.querySelector("#choice-custom").addEventListener("click", async () => {
  state = chooseOwnStations(state);
  render();
  await loadDirectory();
  renderSlotStrip();
  renderStationList();
});
document.querySelector("#stations-back").addEventListener("click", () => { state = restartStationChoice(state); render(); });
document.querySelector("#station-search").addEventListener("input", () => renderStationList());
document.querySelector("#stations-done").addEventListener("click", async () => {
  await postSlots(state.slots);
  state = consoleMode ? {...state, stage: OnboardingStages.CONSOLE_SAVED}
                      : confirmStationChoice(state);
  render();
});
document.querySelector("#manual-network").addEventListener("click", () => { state = selectManualNetwork(state); render(); });
document.querySelector("#credentials-back").addEventListener("click", () => { state = restartNetworkSelection(state); passwordInput.value = ""; ssidInput.value = ""; render(); });
document.querySelector("#blocked-back").addEventListener("click", () => { state = restartNetworkSelection(state); render(); });
document.querySelector("#toggle-password").addEventListener("click", event => {
  const visible = passwordInput.type === "text";
  passwordInput.type = visible ? "password" : "text";
  event.currentTarget.setAttribute("aria-pressed", String(!visible));
  event.currentTarget.textContent = copy[state.locale][visible ? "show" : "hide"];
});
document.querySelector("#credentials-form").addEventListener("submit", async event => {
  event.preventDefault();
  const password = passwordInput.value;
  const ssid = state.manualNetwork ? ssidInput.value.trim() : state.selectedNetworkId;
  const nextState = submitCredentialLength(state, password.length);
  if (nextState.validationError !== null || !ssid || ssid.length > 32) {
    state = {...state, validationError: nextState.validationError ?? "SSID_LENGTH"};
    render();
    return;
  }
  if (deviceApi && ssid) {
    try {
      const response = await fetch("/api/config-form", {
        method: "POST",
        headers: {
          "content-type": "application/x-www-form-urlencoded",
          "X-Open-Radio-CSRF": csrfToken
        },
        body: new URLSearchParams({ssid, password})
      });
      const result = await response.json();
      if (!response.ok || result.accepted !== true) {
        state = {...state, validationError: "DEVICE_REJECTED"};
        render();
        return;
      }
      // The device path ends here, and it has to. In AP+STA mode the ESP32's
      // soft access point is forced onto the router's channel the moment the
      // station interface associates, which drops every phone attached to it.
      // Whatever the page does next, the phone is no longer on the network
      // that serves it — so the steps after this one were never reachable on
      // hardware, no matter how long the portal stayed up. Say what actually
      // happens instead of pretending there is more to tap.
      state = result.waitingForNetwork === true
        ? {...nextState, stage: OnboardingStages.WAITING_HOTSPOT, firstSoundStarted: false, cityMode: null}
        : {...nextState, stage: OnboardingStages.COMPLETE, firstSoundStarted: true,
           cityMode: "AUTO_APPROXIMATE", bluetoothMode: "PAIR_LATER"};
    } catch {
      state = {...state, validationError: "DEVICE_REJECTED"};
      render();
      return;
    }
  }
  passwordInput.value = "";
  ssidInput.value = "";
  if (!deviceApi) state = nextState;
  render();
});
document.querySelector("#city-auto").addEventListener("click", () => { state = chooseCityMode(state, "AUTO_APPROXIMATE"); render(); });
document.querySelector("#city-skip").addEventListener("click", () => { state = chooseCityMode(state, "DISABLED"); render(); });
document.querySelector("#bluetooth-later").addEventListener("click", () => { state = chooseBluetoothMode(state, "PAIR_LATER"); render(); });
document.querySelector("#bluetooth-skip").addEventListener("click", () => { state = chooseBluetoothMode(state, "LOCAL_SPEAKER"); render(); });

window.__onboardingAudit = () => ({
  locale: state.locale,
  stage: state.stage,
  firstSoundStarted: state.firstSoundStarted,
  cityMode: state.cityMode,
  bluetoothMode: state.bluetoothMode,
  passwordRetained: passwordInput.value.length > 0,
  ssidRetained: ssidInput.value.length > 0,
  localStorageKeys: Object.keys(localStorage),
  sessionStorageKeys: Object.keys(sessionStorage),
  locationSearch: location.search,
  locationHash: location.hash
});

async function loadDeviceNetworks(attempt = 0) {
  try {
    const response = await fetch("/api/networks", {cache: "no-store"});
    if (!response.ok || !response.headers.get("content-type")?.includes("application/json")) return;
    const deviceCsrfToken = response.headers.get("X-Open-Radio-CSRF");
    if (!deviceCsrfToken) return;
    const payload = await response.json();
    if (!Array.isArray(payload.networks)) return;
    if (payload.scanning === true && payload.networks.length === 0 && attempt < 10) {
      networks = [];
      csrfToken = deviceCsrfToken;
      deviceApi = true;
      render();
      setTimeout(() => loadDeviceNetworks(attempt + 1), 500);
      return;
    }
    networks = payload.networks.map(network => ({
      ...network,
      label: {pl: network.label, en: network.label}
    }));
    csrfToken = deviceCsrfToken;
    deviceApi = true;
  } catch {
    deviceApi = false;
  }
  render();
}

// Which flow is this page in? Over the AP portal /api/session says "portal"
// (or does not exist on older firmware); over the home network it says
// "console" and the page starts a heartbeat so the device can end the
// session — and resume playing — within a minute of this tab closing.
async function detectSession() {
  try {
    const response = await fetch("/api/session", {cache: "no-store"});
    if (!response.ok) return;
    const token = response.headers.get("X-Open-Radio-CSRF");
    if (token) { csrfToken = token; deviceApi = true; }
    const payload = await response.json();
    consoleMode = payload.mode === "console";
    if (consoleMode) {
      setInterval(() => { fetch("/api/session", {cache: "no-store"}).catch(() => {}); }, 20000);
    }
  } catch {
    consoleMode = false;
  }
}

render();
detectSession().then(() => {
  // The console never needs the Wi-Fi list; scanning is portal work.
  if (!consoleMode) loadDeviceNetworks();
  render();
});
)OPEN_RADIO_ASSET";

struct OnboardingAsset {
  const char* path;
  ContentType contentType;
  const char* content;
  std::size_t size;
};

static constexpr OnboardingAsset kOnboardingAssets[] = {
  {"/network-onboarding/index.html", ContentType::Html, k_index_html, sizeof(k_index_html) - 1},
  {"/network-onboarding/styles.css", ContentType::Css, k_styles_css, sizeof(k_styles_css) - 1},
  {"/network-onboarding/state-machine.mjs", ContentType::JavaScript, k_state_machine_mjs, sizeof(k_state_machine_mjs) - 1},
  {"/network-onboarding/app.mjs", ContentType::JavaScript, k_app_mjs, sizeof(k_app_mjs) - 1}
};

static constexpr std::size_t kOnboardingAssetCount =
    sizeof(kOnboardingAssets) / sizeof(kOnboardingAssets[0]);

}
}
