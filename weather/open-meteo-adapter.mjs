import {requestJson} from "../autonomy/json-request.mjs";

export const OPEN_METEO_URL = "https://api.open-meteo.com/v1/forecast";

export function buildWeatherUrl({latitude, longitude}) {
  const url = new URL(OPEN_METEO_URL);
  url.searchParams.set("latitude", String(latitude));
  url.searchParams.set("longitude", String(longitude));
  url.searchParams.set("current", "temperature_2m,weather_code");
  url.searchParams.set("hourly", "precipitation_probability");
  url.searchParams.set("forecast_hours", "6");
  url.searchParams.set("timezone", "auto");
  return url.toString();
}

export function parseWeather(payload, nowEpochSeconds) {
  const temperatureC = payload?.current?.temperature_2m;
  const weatherCode = payload?.current?.weather_code;
  const times = payload?.hourly?.time;
  const probabilities = payload?.hourly?.precipitation_probability;
  if (!Number.isFinite(temperatureC) || !Number.isInteger(weatherCode) || !Array.isArray(times) || !Array.isArray(probabilities) || times.length !== probabilities.length || times.length === 0) {
    throw Object.assign(new Error("weather response is invalid"), {code: "INVALID_RESPONSE"});
  }
  const bounded = probabilities.slice(0, 3).map(value => Number.isFinite(value) ? Math.max(0, Math.min(100, Math.round(value))) : 0);
  const precipitationProbability = Math.max(...bounded);
  const warningIndex = bounded.findIndex(value => value >= 60);
  return {
    temperatureC: Math.round(temperatureC),
    weatherCode,
    precipitationProbability,
    precipitationInMinutes: warningIndex < 0 ? null : warningIndex * 60,
    warning: warningIndex < 0 ? null : "PRECIPITATION_SOON",
    timezone: typeof payload.timezone === "string" ? payload.timezone : null,
    observedAtEpochSeconds: nowEpochSeconds
  };
}

export async function lookupWeather({fetchImpl, latitude, longitude, nowEpochSeconds, timeoutMilliseconds = 3500}) {
  const payload = await requestJson({url: buildWeatherUrl({latitude, longitude}), fetchImpl, timeoutMilliseconds});
  return parseWeather(payload, nowEpochSeconds);
}
