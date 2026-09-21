// candidates[] documents the broadcaster's official surfaces and is therefore
// HTTPS-only and forbids transient CDN hosts. playback[] is the opposite: the
// exact plain-HTTP edges the device opens. Under an active A2DP link a TLS
// handshake needs ~40 KB of internal heap and only ~25-30 KB is free, so an
// https playback endpoint is not a stricter choice here — it is an unreachable
// one. The two arrays share a station and nothing else.
const transports = new Set(["MP3_ICY"]);
const resolvers = new Set(["DIRECT", "RMFON_POOL"]);

export function validatePlaybackEndpoint(endpoint, approvedHosts) {
  const errors = [];
  if (!endpoint || typeof endpoint !== "object" || Array.isArray(endpoint)) return {valid: false, errors: ["playback endpoint must be an object"]};
  if (typeof endpoint.id !== "string" || endpoint.id.length < 3) errors.push("playback id is invalid");
  if (!transports.has(endpoint.transport)) errors.push("playback transport is invalid");
  if (!resolvers.has(endpoint.resolver)) errors.push("playback resolver is invalid");
  if (!Number.isInteger(endpoint.bitrateKbps) || endpoint.bitrateKbps < 32 || endpoint.bitrateKbps > 320) errors.push("playback bitrate is invalid");
  if (!/^\d{4}-\d{2}-\d{2}$/.test(endpoint.verifiedAt || "")) errors.push("playback verification date is invalid");
  if (typeof endpoint.evidence !== "string" || endpoint.evidence.length < 3) errors.push("playback evidence is invalid");

  let parsed;
  try {
    parsed = new URL(endpoint.url);
  } catch {
    return {valid: false, errors: [...errors, "playback URL is invalid"]};
  }
  // start() rejects anything at or above 128 characters, silently falling back
  // to last-known-good. Catch it here, where the message can say why.
  if (endpoint.url.length > 127) errors.push("playback URL must be at most 127 characters");
  if (parsed.protocol !== "http:") errors.push("playback URL must use plain http (TLS is unreachable under active A2DP)");
  if (parsed.username || parsed.password) errors.push("playback URL must not contain credentials");
  if (!approvedHosts.has(parsed.hostname)) errors.push(`playback host ${parsed.hostname} is not in the firmware network lock`);
  return {valid: errors.length === 0, errors};
}

export function validatePlaybackList(station, approvedHosts, ceilingKbps) {
  const errors = [];
  const endpoints = station.playback || [];
  if (endpoints.length < 1) return [`${station.id}: at least one playback endpoint is required`];
  const ids = new Set();
  for (const endpoint of endpoints) {
    for (const error of validatePlaybackEndpoint(endpoint, approvedHosts).errors) errors.push(`${station.id}/${endpoint.id || "endpoint"}: ${error}`);
    if (ids.has(endpoint.id)) errors.push(`${station.id}: duplicate playback id ${endpoint.id}`);
    ids.add(endpoint.id);
  }
  // A station is admissible only if it can play under the bitrate we have
  // actually measured under an active Bluetooth link. A higher-bitrate
  // alternate is allowed; a station that offers nothing else is not.
  if (!endpoints.some(endpoint => endpoint.bitrateKbps <= ceilingKbps)) {
    errors.push(`${station.id}: every playback endpoint exceeds the measured ${ceilingKbps} kb/s ceiling`);
  }
  return errors;
}
