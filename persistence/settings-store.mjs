import {WritePhases, applyAtomicWrite, selectBootConfig} from "./persistence.mjs";
import {validateCurrentConfig} from "./config-contract.mjs";
import {defaultDisplayProfile} from "../display/display-policy.mjs";

const emptySlots = () => ({slots: {A: null, B: null}});

export function createDefaultDeviceConfig(preferredStationId) {
  return {
    schemaVersion: 2,
    locale: "pl",
    preferredStationId,
    volume: 50,
    brightness: 75,
    theme: "dark",
    nowPlayingVariant: "editorial",
    display: structuredClone(defaultDisplayProfile),
    wifiProfileRefs: ["wifi:simulator"],
    bluetoothDeviceRef: null,
    city: {mode: "AUTO_APPROXIMATE", label: null},
    onboardingComplete: true
  };
}

export function createSettingsStore({
  backingStore,
  allowedStationIds,
  defaultConfig = createDefaultDeviceConfig(allowedStationIds[0]),
  key = "open-radio-core2.device-config.v2",
  now = () => Date.now()
}) {
  const validation = validateCurrentConfig(defaultConfig, allowedStationIds);
  if (!validation.valid) throw new Error(`invalid default settings: ${validation.errors.join("; ")}`);

  function readSlots() {
    try {
      const serialized = backingStore.getItem(key);
      return serialized === null ? emptySlots() : JSON.parse(serialized);
    } catch {
      return emptySlots();
    }
  }

  function writeSlots(storage) {
    backingStore.setItem(key, JSON.stringify(storage));
  }

  function load() {
    const selected = selectBootConfig(readSlots(), allowedStationIds);
    if (selected.config !== null) return selected.config;
    const seeded = applyAtomicWrite(emptySlots(), defaultConfig, WritePhases.AFTER_COMMIT, now(), allowedStationIds);
    writeSlots(seeded.storage);
    return structuredClone(defaultConfig);
  }

  function save(patch) {
    const storage = readSlots();
    const selected = selectBootConfig(storage, allowedStationIds);
    const current = selected.config ?? defaultConfig;
    const next = {...current, ...structuredClone(patch)};
    const written = applyAtomicWrite(storage, next, WritePhases.AFTER_COMMIT, now(), allowedStationIds);
    writeSlots(written.storage);
    return structuredClone(next);
  }

  return Object.freeze({load, save});
}
