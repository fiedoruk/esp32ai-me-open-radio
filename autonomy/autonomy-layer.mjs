import {createMemoryServiceCache, runBoundedService} from "./bounded-service.mjs";
import {lookupIpLocation} from "../location/ip-location-adapter.mjs";
import {lookupWeather} from "../weather/open-meteo-adapter.mjs";
import {synchronizeOptionalTime} from "../time/time-sync-supervisor.mjs";

const localeForCountry = countryCode => countryCode === "PL" ? "pl" : "en";

export function createAutonomyLayer({
  fetchImpl = globalThis.fetch,
  syncTimeImpl = null,
  cache = createMemoryServiceCache(),
  now = () => Math.floor(Date.now() / 1000),
  sleep
} = {}) {
  async function run({approvedWifiProfileRef, force = false}) {
    if (typeof approvedWifiProfileRef !== "string" || !approvedWifiProfileRef.startsWith("wifi:")) throw new Error("approved Wi-Fi profile reference is required");
    const nowEpochSeconds = now();
    const location = await runBoundedService({
      serviceId: "ip-location",
      cacheKey: `location:${approvedWifiProfileRef}`,
      cache,
      nowEpochSeconds,
      freshForSeconds: 2592000,
      maximumStaleSeconds: 7776000,
      maximumAttempts: 2,
      backoffSeconds: [0],
      sleep,
      force,
      execute: () => lookupIpLocation({fetchImpl, nowEpochSeconds})
    });
    const weather = location.data === null
      ? {serviceId: "weather", status: "SKIPPED_NO_LOCATION", data: null, ageSeconds: null, attempts: 0, errorCode: null}
      : await runBoundedService({
        serviceId: "weather",
        cacheKey: `weather:${location.data.latitude.toFixed(2)}:${location.data.longitude.toFixed(2)}`,
        cache,
        nowEpochSeconds,
        freshForSeconds: 1200,
        maximumStaleSeconds: 21600,
        maximumAttempts: 2,
        backoffSeconds: [0],
        sleep,
        force,
        execute: () => lookupWeather({fetchImpl, latitude: location.data.latitude, longitude: location.data.longitude, nowEpochSeconds})
      });
    const time = await synchronizeOptionalTime({syncImpl: syncTimeImpl, timezone: location.data?.timezone ?? "UTC", nowEpochSeconds});
    return {
      status: location.data === null ? "DEGRADED" : "READY",
      location,
      weather,
      time,
      suggestions: location.data === null ? null : {
        locale: localeForCountry(location.data.countryCode),
        countryCode: location.data.countryCode,
        timezone: location.data.timezone,
        stationRegion: location.data.locality,
        weatherCoordinates: {latitude: location.data.latitude, longitude: location.data.longitude}
      }
    };
  }

  return Object.freeze({run});
}
