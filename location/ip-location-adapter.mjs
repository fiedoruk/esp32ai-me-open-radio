import {requestJson} from "../autonomy/json-request.mjs";
import {LocationSources} from "./auto-location.mjs";

export const IP_LOCATION_URL = "https://ipwho.is/";

function boundedNumber(value, minimum, maximum, field) {
  if (!Number.isFinite(value) || value < minimum || value > maximum) throw Object.assign(new Error(`${field} is invalid`), {code: "INVALID_RESPONSE"});
  return value;
}

export function parseIpLocation(payload, nowEpochSeconds) {
  if (!payload || payload.success !== true) throw Object.assign(new Error("provider rejected lookup"), {code: "PROVIDER_REJECTED"});
  const locality = typeof payload.city === "string" && payload.city.trim() ? payload.city.trim() : null;
  const countryCode = typeof payload.country_code === "string" && /^[A-Z]{2}$/.test(payload.country_code) ? payload.country_code : null;
  const timezone = typeof payload.timezone?.id === "string" && payload.timezone.id.includes("/") ? payload.timezone.id : null;
  if (locality === null || countryCode === null || timezone === null) throw Object.assign(new Error("provider fields are incomplete"), {code: "INVALID_RESPONSE"});
  return {
    source: LocationSources.IP_GEOLOCATION,
    latitude: boundedNumber(payload.latitude, -90, 90, "latitude"),
    longitude: boundedNumber(payload.longitude, -180, 180, "longitude"),
    accuracyMeters: 25000,
    locality,
    countryCode,
    timezone,
    observedAtEpochSeconds: nowEpochSeconds
  };
}

export async function lookupIpLocation({fetchImpl, nowEpochSeconds, timeoutMilliseconds = 3500}) {
  const payload = await requestJson({url: IP_LOCATION_URL, fetchImpl, timeoutMilliseconds});
  return parseIpLocation(payload, nowEpochSeconds);
}
