import {BUILT_IN_STATION, OnboardingStages, chooseBluetoothMode, chooseCityMode, chooseDefaultNine, chooseOwnStations, confirmStationChoice, createOnboardingState, restartNetworkSelection, restartStationChoice, selectManualNetwork, selectOnboardingNetwork, submitCredentialLength, toggleSlot, toggleStationChoice} from "./state-machine.mjs";

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
