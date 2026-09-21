import {defaultDisplayProfile, validateDisplayProfile} from "../display/display-policy.mjs";

const locales = new Set(["pl", "en"]);
const themes = new Set(["dark", "light"]);
const nowPlayingVariants = new Set(["editorial", "glance", "air"]);
const cityModes = new Set(["AUTO_APPROXIMATE", "MANUAL", "DISABLED"]);
const currentKeys = new Set(["schemaVersion", "locale", "preferredStationId", "volume", "brightness", "theme", "nowPlayingVariant", "display", "wifiProfileRefs", "bluetoothDeviceRef", "city", "onboardingComplete"]);
const legacyKeys = new Set(["schemaVersion", "language", "stationId", "volume", "wifiProfileRef", "bluetoothDeviceRef", "cityLabel", "onboardingComplete"]);
const referencePattern = /^(wifi|bt):[a-z0-9][a-z0-9-]{0,47}$/;
const stationPattern = /^[a-z0-9][a-z0-9-]{2,63}$/;

function isPlainObject(value) {
  return Boolean(value) && typeof value === "object" && !Array.isArray(value);
}

function unexpectedKeys(value, allowed) {
  return Object.keys(value).filter(key => !allowed.has(key));
}

function validateStationId(stationId, allowedStationIds, errors) {
  if (typeof stationId !== "string" || !stationPattern.test(stationId)) errors.push("preferred station id is invalid");
  else if (allowedStationIds.length && !allowedStationIds.includes(stationId)) errors.push("preferred station is not in the embedded catalog");
}

export function validateCurrentConfig(config, allowedStationIds = []) {
  const errors = [];
  if (!isPlainObject(config)) return {valid: false, errors: ["config must be an object"]};
  if (config.schemaVersion !== 2) errors.push("config schemaVersion must be 2");
  const extras = unexpectedKeys(config, currentKeys);
  if (extras.length) errors.push(`unexpected config keys: ${extras.join(",")}`);
  if (!locales.has(config.locale)) errors.push("locale is invalid");
  validateStationId(config.preferredStationId, allowedStationIds, errors);
  if (!Number.isInteger(config.volume) || config.volume < 0 || config.volume > 100) errors.push("volume must be an integer from 0 to 100");
  if (!Number.isInteger(config.brightness) || config.brightness < 0 || config.brightness > 100) errors.push("brightness must be an integer from 0 to 100");
  if (!themes.has(config.theme)) errors.push("theme is invalid");
  if (!nowPlayingVariants.has(config.nowPlayingVariant)) errors.push("now-playing variant is invalid");
  const displayValidation = validateDisplayProfile(config.display);
  if (!displayValidation.valid) errors.push(...displayValidation.errors.map(error => `display: ${error}`));
  if (!Array.isArray(config.wifiProfileRefs) || config.wifiProfileRefs.length < 1 || config.wifiProfileRefs.length > 8) errors.push("wifiProfileRefs must contain 1 to 8 references");
  else {
    if (new Set(config.wifiProfileRefs).size !== config.wifiProfileRefs.length) errors.push("wifiProfileRefs must be unique");
    if (config.wifiProfileRefs.some(reference => typeof reference !== "string" || !reference.startsWith("wifi:") || !referencePattern.test(reference))) errors.push("wifiProfileRefs contain an invalid reference");
  }
  if (config.bluetoothDeviceRef !== null && (typeof config.bluetoothDeviceRef !== "string" || !config.bluetoothDeviceRef.startsWith("bt:") || !referencePattern.test(config.bluetoothDeviceRef))) errors.push("bluetoothDeviceRef is invalid");
  if (!isPlainObject(config.city) || !cityModes.has(config.city.mode) || !Object.hasOwn(config.city, "label")) errors.push("city is invalid");
  else {
    if (unexpectedKeys(config.city, new Set(["mode", "label"])).length) errors.push("city contains unexpected keys");
    if (config.city.label !== null && (typeof config.city.label !== "string" || config.city.label.length < 1 || config.city.label.length > 64)) errors.push("city label is invalid");
    if (config.city.mode === "MANUAL" && config.city.label === null) errors.push("manual city requires a label");
    if (config.city.mode === "DISABLED" && config.city.label !== null) errors.push("disabled city must not keep a label");
  }
  if (typeof config.onboardingComplete !== "boolean") errors.push("onboardingComplete must be boolean");
  return {valid: errors.length === 0, errors};
}

export function validateLegacyConfig(config, allowedStationIds = []) {
  const errors = [];
  if (!isPlainObject(config)) return {valid: false, errors: ["legacy config must be an object"]};
  if (config.schemaVersion !== 1) errors.push("legacy config schemaVersion must be 1");
  const extras = unexpectedKeys(config, legacyKeys);
  if (extras.length) errors.push(`unexpected legacy keys: ${extras.join(",")}`);
  if (!locales.has(config.language)) errors.push("legacy language is invalid");
  validateStationId(config.stationId, allowedStationIds, errors);
  if (!Number.isInteger(config.volume) || config.volume < 0 || config.volume > 100) errors.push("legacy volume is invalid");
  if (typeof config.wifiProfileRef !== "string" || !config.wifiProfileRef.startsWith("wifi:") || !referencePattern.test(config.wifiProfileRef)) errors.push("legacy Wi-Fi reference is invalid");
  if (config.bluetoothDeviceRef !== null && (typeof config.bluetoothDeviceRef !== "string" || !config.bluetoothDeviceRef.startsWith("bt:") || !referencePattern.test(config.bluetoothDeviceRef))) errors.push("legacy Bluetooth reference is invalid");
  if (config.cityLabel !== null && (typeof config.cityLabel !== "string" || config.cityLabel.length < 1 || config.cityLabel.length > 64)) errors.push("legacy city label is invalid");
  if (typeof config.onboardingComplete !== "boolean") errors.push("legacy onboardingComplete must be boolean");
  return {valid: errors.length === 0, errors};
}

export function migrateConfig(config, allowedStationIds = []) {
  if (!isPlainObject(config) || !Number.isInteger(config.schemaVersion)) return {valid: false, reason: "SCHEMA_VERSION_MISSING"};
  if (config.schemaVersion > 2) return {valid: false, reason: "FUTURE_SCHEMA_UNSUPPORTED"};
  if (config.schemaVersion < 1) return {valid: false, reason: "SCHEMA_VERSION_UNSUPPORTED"};
  if (config.schemaVersion === 2) {
    const normalized = {
      ...structuredClone(config),
      brightness: config.brightness ?? 75,
      theme: config.theme ?? "dark",
      nowPlayingVariant: config.nowPlayingVariant ?? "editorial",
      display: structuredClone(config.display ?? defaultDisplayProfile)
    };
    const validation = validateCurrentConfig(normalized, allowedStationIds);
    return validation.valid ? {valid: true, config: normalized, migratedFromVersion: null} : {valid: false, reason: "CONFIG_INVALID", errors: validation.errors};
  }
  const legacyValidation = validateLegacyConfig(config, allowedStationIds);
  if (!legacyValidation.valid) return {valid: false, reason: "LEGACY_CONFIG_INVALID", errors: legacyValidation.errors};
  const migrated = {
    schemaVersion: 2,
    locale: config.language,
    preferredStationId: config.stationId,
    volume: config.volume,
    brightness: 75,
    theme: "dark",
    nowPlayingVariant: "editorial",
    display: structuredClone(defaultDisplayProfile),
    wifiProfileRefs: [config.wifiProfileRef],
    bluetoothDeviceRef: config.bluetoothDeviceRef,
    city: {mode: config.cityLabel === null ? "DISABLED" : "MANUAL", label: config.cityLabel},
    onboardingComplete: config.onboardingComplete
  };
  const currentValidation = validateCurrentConfig(migrated, allowedStationIds);
  return currentValidation.valid ? {valid: true, config: migrated, migratedFromVersion: 1} : {valid: false, reason: "MIGRATION_INVALID", errors: currentValidation.errors};
}
