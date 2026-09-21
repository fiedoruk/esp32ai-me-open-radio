export const LocationSources = Object.freeze({
  MANUAL_OVERRIDE: "MANUAL_OVERRIDE",
  SAVED_NETWORK_LOCATION: "SAVED_NETWORK_LOCATION",
  ONBOARDING_DEVICE_GEOLOCATION: "ONBOARDING_DEVICE_GEOLOCATION",
  WIFI_POSITIONING: "WIFI_POSITIONING",
  IP_GEOLOCATION: "IP_GEOLOCATION",
  COUNTRY_DEFAULT: "COUNTRY_DEFAULT"
});

const sourceRanks = new Map(Object.values(LocationSources).map((source, rank) => [source, rank]));

function normalizeCandidate(candidate) {
  if (!candidate || typeof candidate !== "object" || !sourceRanks.has(candidate.source)) return null;
  if (!Number.isFinite(candidate.latitude) || candidate.latitude < -90 || candidate.latitude > 90) return null;
  if (!Number.isFinite(candidate.longitude) || candidate.longitude < -180 || candidate.longitude > 180) return null;
  if (!Number.isFinite(candidate.accuracyMeters) || candidate.accuracyMeters < 0) return null;
  if (!Number.isInteger(candidate.resolvedAtEpochSeconds) || candidate.resolvedAtEpochSeconds < 0) return null;
  return structuredClone(candidate);
}

export function selectBestLocation(candidates, {nowEpochSeconds, maximumAgeSeconds = 2592000}) {
  if (!Array.isArray(candidates)) throw new Error("location candidates must be an array");
  if (!Number.isInteger(nowEpochSeconds) || nowEpochSeconds < 0) throw new Error("current time is invalid");
  if (!Number.isInteger(maximumAgeSeconds) || maximumAgeSeconds < 1) throw new Error("maximum age is invalid");

  const usable = candidates.map(normalizeCandidate).filter(Boolean).filter(candidate => {
    if (candidate.source === LocationSources.MANUAL_OVERRIDE) return true;
    const age = nowEpochSeconds - candidate.resolvedAtEpochSeconds;
    return age >= 0 && age <= maximumAgeSeconds;
  });
  usable.sort((left, right) => sourceRanks.get(left.source) - sourceRanks.get(right.source)
    || left.accuracyMeters - right.accuracyMeters
    || right.resolvedAtEpochSeconds - left.resolvedAtEpochSeconds);

  const selected = usable[0] ?? null;
  if (selected === null) return {status: "UNAVAILABLE", selected: null};
  return {status: "SELECTED", selected};
}
