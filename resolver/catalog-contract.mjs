import {validatePlaybackList} from "./playback-contract.mjs";

const roles = new Set(["PRIMARY", "ALTERNATE"]);
const kinds = new Set(["OFFICIAL_API", "OFFICIAL_REDIRECT", "OFFICIAL_PLAYER", "OFFICIAL_PAGE"]);
const parsers = new Set(["RMFON_JSON", "EUROZET_JSON", "TUBA_JSON", "TOKFM_PLAYER", "ZPR_PLAYER", "HTML_REFERENCE", "DIRECT_STREAM", "HTTP_REDIRECT", "HLS_MASTER"]);
const capabilities = new Set(["MP3_ICY", "HLS_HE_AAC"]);
const feasibility = new Set(["FEASIBLE", "PARSER_AUDIT_REQUIRED", "REFERENCE_ONLY"]);
const transports = new Set(["MP3", "AAC", "HLS_HE_AAC"]);
const transientHostPatterns = [
  /(^|\.)rmfstream\.pl$/i,
  /(^|\.)streamtheworld\.com$/i,
  /(^|\.)cdn([.-]|$)/i,
  /(^|\.)edge([.-]|$)/i
];

function validateDiscoveryUrl(value) {
  if (typeof value !== "string") return "candidate URL must use HTTPS";
  try {
    const parsed = new URL(value);
    if (parsed.protocol !== "https:" || !parsed.hostname.includes(".")) return "candidate URL must use HTTPS";
    if (parsed.username || parsed.password) return "candidate URL must not contain credentials";
    if (transientHostPatterns.some(pattern => pattern.test(parsed.hostname))) return "transient playback host must not be canonical";
    return null;
  } catch {
    return "candidate URL is invalid";
  }
}

export function validateResolverCandidate(candidate) {
  const errors = [];
  if (!candidate || typeof candidate !== "object" || Array.isArray(candidate)) return {valid: false, errors: ["candidate must be an object"]};
  if (typeof candidate.id !== "string" || candidate.id.length < 3) errors.push("candidate id is invalid");
  if (!roles.has(candidate.role)) errors.push("candidate role is invalid");
  if (!kinds.has(candidate.kind)) errors.push("candidate kind is invalid");
  const urlError = validateDiscoveryUrl(candidate.url);
  if (urlError) errors.push(urlError);
  if (typeof candidate.owner !== "string" || candidate.owner.length < 2) errors.push("candidate owner is invalid");
  if (!parsers.has(candidate.parser)) errors.push("candidate parser is invalid");
  if (!Array.isArray(candidate.transportPreference) || candidate.transportPreference.length < 1 || candidate.transportPreference.some(item => !transports.has(item))) errors.push("candidate transport preference is invalid");
  if (candidate.persistence !== "DISCOVERY_ONLY") errors.push("candidate persistence must be DISCOVERY_ONLY");
  if (!feasibility.has(candidate.deviceFeasibility)) errors.push("candidate feasibility is invalid");
  if (!/^\d{4}-\d{2}-\d{2}$/.test(candidate.verifiedAt || "")) errors.push("candidate verification date is invalid");
  if (typeof candidate.evidence !== "string" || candidate.evidence.length < 3) errors.push("candidate evidence is invalid");
  return {valid: errors.length === 0, errors};
}

export function validateStationCatalog(catalog, expectedIds = [], approvedHosts = null) {
  const errors = [];
  if (!catalog || typeof catalog !== "object") return {valid: false, errors: ["catalog must be an object"]};
  if (catalog.schemaVersion !== 1) errors.push("catalog schemaVersion must be 1");
  if (catalog.lifecycle !== "EMBEDDED_STATIC" || catalog.remoteUpdate !== false) errors.push("catalog lifecycle must be embedded and static");
  if (!Array.isArray(catalog.stations) || catalog.stations.length < 1 || catalog.stations.length > 9) errors.push("catalog must contain one to nine stations");
  const stationIds = new Set();
  const candidateIds = new Set();
  const capabilityCounts = {MP3_ICY: 0, HLS_HE_AAC: 0};
  for (const station of catalog.stations || []) {
    if (stationIds.has(station.id)) errors.push(`duplicate station id: ${station.id}`);
    stationIds.add(station.id);
    if (!capabilities.has(station.capabilityClass)) errors.push(`${station.id}: invalid capability class`);
    else capabilityCounts[station.capabilityClass] += 1;
    if (station.capabilityClass === "MP3_ICY" && !new Set(["MODEL_READY", "PARSER_PENDING"]).has(station.firmwareSupport)) errors.push(`${station.id}: MP3 firmware support is inconsistent`);
    if (station.capabilityClass === "HLS_HE_AAC" && station.firmwareSupport !== "DECODER_PENDING") errors.push(`${station.id}: HLS firmware support is inconsistent`);
    if (station.artworkRights !== "PROJECT_ORIGINAL_ONLY") errors.push(`${station.id}: artwork rights are not project-original-only`);
    if (station.nameUse !== "IDENTIFICATION_ONLY") errors.push(`${station.id}: station name use is too broad`);
    if (!/^[A-Z]{2}$/.test(station.country || "")) errors.push(`${station.id}: country code is invalid`);
    if (typeof station.language !== "string" || station.language.length < 2 || station.language.length > 16) errors.push(`${station.id}: language tag is invalid`);
    if (!Array.isArray(station.candidates) || station.candidates.length < 2 || station.candidates.length > 4) errors.push(`${station.id}: requires primary and alternate candidates`);
    const stationRoles = new Set();
    for (const candidate of station.candidates || []) {
      const validation = validateResolverCandidate(candidate);
      for (const error of validation.errors) errors.push(`${station.id}/${candidate.id || "candidate"}: ${error}`);
      if (candidateIds.has(candidate.id)) errors.push(`duplicate candidate id: ${candidate.id}`);
      candidateIds.add(candidate.id);
      stationRoles.add(candidate.role);
    }
    if (!stationRoles.has("PRIMARY") || !stationRoles.has("ALTERNATE")) errors.push(`${station.id}: candidate roles are incomplete`);
    if (approvedHosts) {
      for (const error of validatePlaybackList(station, approvedHosts, catalog.bitrateCeilingKbps ?? 128)) errors.push(error);
    }
  }
  if (expectedIds.length && JSON.stringify([...stationIds]) !== JSON.stringify(expectedIds)) errors.push("canonical and UI station order differ");
  return {valid: errors.length === 0, errors, capabilityCounts};
}
