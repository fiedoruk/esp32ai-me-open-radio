export const OnboardingStages = Object.freeze({
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
