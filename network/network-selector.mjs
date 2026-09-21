import {validateNetworkInputs, validateRememberedProfile} from "./network-contract.mjs";

export const NetworkOutcomes = Object.freeze({
  CONNECTED: "CONNECTED",
  TIMEOUT: "TIMEOUT",
  AUTH_FAILURE: "AUTH_FAILURE",
  NO_ROUTE: "NO_ROUTE",
  CAPTIVE_DETECTED: "CAPTIVE_DETECTED"
});

const securedModes = new Set(["WPA2_PSK", "WPA3_SAE"]);
const backoffMs = Object.freeze([5_000, 30_000, 120_000, 600_000]);
const recentSuccessWindowMs = 86_400_000;

function clamp(value, minimum, maximum) {
  return Math.max(minimum, Math.min(maximum, value));
}

export function scoreNetwork(profile, scan, nowMs) {
  const signalScore = clamp(scan.rssi + 100, 0, 100);
  const preferredBonus = profile.preferred ? 30 : 0;
  const timeSinceSuccess = profile.lastSuccessMs === null ? null : nowMs - profile.lastSuccessMs;
  const recentBonus = timeSinceSuccess !== null && timeSinceSuccess >= 0 && timeSinceSuccess <= recentSuccessWindowMs ? 10 : 0;
  return profile.priority + profile.healthScore + signalScore + preferredBonus + recentBonus - profile.consecutiveFailures * 15;
}

function promptReason(scan, profile) {
  if (scan.security === "OPEN") return "OPEN_NETWORK_REQUIRES_CONFIRMATION";
  if (scan.captivePortal) return "CAPTIVE_PORTAL_REQUIRES_LOCAL_FLOW";
  if (scan.security === "UNKNOWN") return "UNKNOWN_SECURITY_REQUIRES_CONFIRMATION";
  if (!profile) return "UNKNOWN_NETWORK_REQUIRES_ONBOARDING";
  if (!profile.approved) return "UNAPPROVED_PROFILE_REQUIRES_CONFIRMATION";
  return null;
}

export function selectKnownNetwork({profiles, scans, nowMs}) {
  if (!Number.isInteger(nowMs) || nowMs < 0) throw new Error("nowMs must be a non-negative integer");
  const validation = validateNetworkInputs(profiles, scans);
  if (!validation.valid) throw new Error(`invalid network inputs: ${validation.errors.join("; ")}`);
  const byReference = new Map(profiles.map(profile => [profile.ref, profile]));
  const trace = [];
  const candidates = [];
  let promptRequired = false;
  for (const scan of scans) {
    const profile = scan.profileRef === null ? null : byReference.get(scan.profileRef) || null;
    if (!scan.reachable) {
      trace.push({scanId: scan.scanId, profileRef: scan.profileRef, decision: "REJECT", reason: "UNREACHABLE", score: null});
      continue;
    }
    const reason = promptReason(scan, profile);
    if (reason) {
      promptRequired = true;
      trace.push({scanId: scan.scanId, profileRef: scan.profileRef, decision: "PROMPT", reason, score: null});
      continue;
    }
    if (!securedModes.has(scan.security)) {
      promptRequired = true;
      trace.push({scanId: scan.scanId, profileRef: scan.profileRef, decision: "PROMPT", reason: "SECURITY_NOT_AUTOMATIC", score: null});
      continue;
    }
    if (profile.quarantineUntilMs > nowMs) {
      trace.push({scanId: scan.scanId, profileRef: profile.ref, decision: "REJECT", reason: "QUARANTINED", score: null});
      continue;
    }
    const score = scoreNetwork(profile, scan, nowMs);
    candidates.push({profile, scan, score});
    trace.push({scanId: scan.scanId, profileRef: profile.ref, decision: "CANDIDATE", reason: null, score});
  }
  candidates.sort((left, right) => right.score - left.score || left.profile.ref.localeCompare(right.profile.ref));
  if (candidates.length) {
    const selected = candidates[0];
    return {
      schemaVersion: 1,
      status: "SELECTED",
      selectedProfileRef: selected.profile.ref,
      retryAtMs: null,
      trace: trace.map(entry => entry.profileRef === selected.profile.ref && entry.decision === "CANDIDATE" ? {...entry, decision: "SELECT"} : entry)
    };
  }
  if (promptRequired) return {schemaVersion: 1, status: "PROMPT_REQUIRED", selectedProfileRef: null, retryAtMs: null, trace};
  const quarantineDeadlines = profiles.map(profile => profile.quarantineUntilMs).filter(deadline => deadline > nowMs);
  const retryAtMs = quarantineDeadlines.length ? Math.min(...quarantineDeadlines) : nowMs + backoffMs[0];
  return {schemaVersion: 1, status: "RETRY_SCHEDULED", selectedProfileRef: null, retryAtMs, trace};
}

export function applyNetworkOutcome(profile, outcome, nowMs) {
  const validation = validateRememberedProfile(profile);
  if (!validation.valid) throw new Error(`invalid profile: ${validation.errors.join("; ")}`);
  if (!Object.values(NetworkOutcomes).includes(outcome)) throw new Error(`unknown network outcome: ${outcome}`);
  if (!Number.isInteger(nowMs) || nowMs < 0) throw new Error("nowMs must be a non-negative integer");
  if (outcome === NetworkOutcomes.CONNECTED) {
    return {...profile, healthScore: clamp(profile.healthScore + 10, 0, 100), consecutiveFailures: 0, quarantineUntilMs: 0, lastSuccessMs: nowMs};
  }
  const consecutiveFailures = profile.consecutiveFailures + 1;
  const delta = outcome === NetworkOutcomes.AUTH_FAILURE || outcome === NetworkOutcomes.CAPTIVE_DETECTED ? -50 : outcome === NetworkOutcomes.NO_ROUTE ? -25 : -20;
  const immediateQuarantine = outcome === NetworkOutcomes.AUTH_FAILURE || outcome === NetworkOutcomes.CAPTIVE_DETECTED;
  const quarantineRequired = immediateQuarantine || consecutiveFailures >= 2;
  const delayIndex = Math.min(Math.max(0, consecutiveFailures - 1), backoffMs.length - 1);
  const delay = immediateQuarantine ? backoffMs.at(-1) : backoffMs[delayIndex];
  return {
    ...profile,
    healthScore: clamp(profile.healthScore + delta, 0, 100),
    consecutiveFailures,
    quarantineUntilMs: quarantineRequired ? nowMs + delay : 0
  };
}
