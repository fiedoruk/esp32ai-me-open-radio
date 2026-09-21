export const CandidateOutcomes = Object.freeze({
  SUCCESS: "SUCCESS",
  TIMEOUT: "TIMEOUT",
  HTTP_404: "HTTP_404",
  STALE: "STALE",
  PARSE_ERROR: "PARSE_ERROR"
});

const outcomePolicy = Object.freeze({
  SUCCESS: {scoreDelta: 20, retryDelayMs: 0},
  TIMEOUT: {scoreDelta: -25, retryDelayMs: 5_000},
  HTTP_404: {scoreDelta: -50, retryDelayMs: 30_000},
  STALE: {scoreDelta: -20, retryDelayMs: 5_000},
  PARSE_ERROR: {scoreDelta: -35, retryDelayMs: 30_000}
});
const boundedBackoffMs = Object.freeze([5_000, 30_000, 120_000, 600_000]);

function defaultCandidateHealth() {
  return {score: 100, consecutiveFailures: 0, quarantineUntilMs: 0, attempts: 0, lastOutcome: null, lastSuccessMs: null};
}

function healthFor(state, candidateId) {
  return state.candidates[candidateId] || defaultCandidateHealth();
}

export function createHealthState(catalog) {
  const candidates = {};
  for (const station of catalog.stations) for (const candidate of station.candidates) candidates[candidate.id] = defaultCandidateHealth();
  return {schemaVersion: 1, candidates};
}

export function applyCandidateOutcome(state, candidateId, outcome, nowMs) {
  const policy = outcomePolicy[outcome];
  if (!policy) throw new Error(`unknown candidate outcome: ${outcome}`);
  if (!Number.isInteger(nowMs) || nowMs < 0) throw new Error("nowMs must be a non-negative integer");
  const previous = healthFor(state, candidateId);
  const success = outcome === CandidateOutcomes.SUCCESS;
  const consecutiveFailures = success ? 0 : previous.consecutiveFailures + 1;
  const score = Math.max(0, Math.min(100, previous.score + policy.scoreDelta));
  const quarantineRequired = !success && (consecutiveFailures >= 2 || score <= 50);
  const backoffIndex = Math.min(Math.max(0, consecutiveFailures - 1), boundedBackoffMs.length - 1);
  const quarantineUntilMs = success ? 0 : quarantineRequired ? nowMs + Math.max(policy.retryDelayMs, boundedBackoffMs[backoffIndex]) : 0;
  const updated = {
    score,
    consecutiveFailures,
    quarantineUntilMs,
    attempts: previous.attempts + 1,
    lastOutcome: outcome,
    lastSuccessMs: success ? nowMs : previous.lastSuccessMs
  };
  return {schemaVersion: 1, candidates: {...state.candidates, [candidateId]: updated}};
}

function syntheticLastKnownGood(stationId, lastKnownGood) {
  if (!lastKnownGood || lastKnownGood.stationId !== stationId || typeof lastKnownGood.endpointId !== "string") return null;
  return {
    id: `lkg:${stationId}`,
    role: "LAST_KNOWN_GOOD",
    deviceFeasibility: "FEASIBLE",
    sourceEndpointId: lastKnownGood.endpointId
  };
}

export function orderCandidates(station, health, nowMs, lastKnownGood = null, preferLastKnownGood = false) {
  const lastGood = syntheticLastKnownGood(station.id, lastKnownGood);
  const catalogCandidates = station.candidates.map(candidate => ({...candidate}));
  catalogCandidates.sort((left, right) => {
    const leftHealth = healthFor(health, left.id);
    const rightHealth = healthFor(health, right.id);
    const leftQuarantined = leftHealth.quarantineUntilMs > nowMs;
    const rightQuarantined = rightHealth.quarantineUntilMs > nowMs;
    if (leftQuarantined !== rightQuarantined) return leftQuarantined ? 1 : -1;
    if (leftHealth.score !== rightHealth.score) return rightHealth.score - leftHealth.score;
    return left.role === "PRIMARY" ? -1 : 1;
  });
  if (!lastGood) return catalogCandidates;
  return preferLastKnownGood ? [lastGood, ...catalogCandidates] : [...catalogCandidates, lastGood];
}

function traceEntry(candidateId, action, outcome, before, after, nowMs) {
  return {
    atMs: nowMs,
    candidateId,
    action,
    outcome,
    scoreBefore: before?.score ?? null,
    scoreAfter: after?.score ?? null,
    quarantineUntilMs: after?.quarantineUntilMs ?? null
  };
}

export function resolveStation({catalog, stationId, health, nowMs, outcomes = {}, lastKnownGood = null, supportedCapabilities = ["MP3_ICY"], preferLastKnownGood = false}) {
  const station = catalog.stations.find(item => item.id === stationId);
  if (!station) throw new Error(`unknown station: ${stationId}`);
  if (!supportedCapabilities.includes(station.capabilityClass)) {
    return {
      result: {schemaVersion: 1, stationId, status: "UNSUPPORTED", selectedCandidateId: null, retryAtMs: null, trace: [{atMs: nowMs, candidateId: null, action: "CAPABILITY_CHECK", outcome: station.capabilityClass}]},
      health
    };
  }
  let nextHealth = health;
  const trace = [];
  const attemptedIds = [];
  const ordered = orderCandidates(station, health, nowMs, lastKnownGood, preferLastKnownGood);
  for (const candidate of ordered) {
    if (candidate.deviceFeasibility === "REFERENCE_ONLY") {
      trace.push(traceEntry(candidate.id, "SKIP", "REFERENCE_ONLY", healthFor(nextHealth, candidate.id), healthFor(nextHealth, candidate.id), nowMs));
      continue;
    }
    const before = healthFor(nextHealth, candidate.id);
    if (before.quarantineUntilMs > nowMs) {
      trace.push(traceEntry(candidate.id, "SKIP", "QUARANTINED", before, before, nowMs));
      continue;
    }
    const outcomeKey = candidate.role === "LAST_KNOWN_GOOD" ? "LAST_KNOWN_GOOD" : candidate.id;
    const outcome = outcomes[outcomeKey] || CandidateOutcomes.TIMEOUT;
    attemptedIds.push(candidate.id);
    nextHealth = applyCandidateOutcome(nextHealth, candidate.id, outcome, nowMs);
    const after = healthFor(nextHealth, candidate.id);
    trace.push(traceEntry(candidate.id, "ATTEMPT", outcome, before, after, nowMs));
    if (outcome === CandidateOutcomes.SUCCESS) {
      return {
        result: {schemaVersion: 1, stationId, status: "PLAYING", selectedCandidateId: candidate.id, retryAtMs: null, trace},
        health: nextHealth
      };
    }
  }
  const retryCandidates = attemptedIds.map(candidateId => healthFor(nextHealth, candidateId).quarantineUntilMs).filter(value => value > nowMs);
  const retryAtMs = retryCandidates.length ? Math.min(...retryCandidates) : nowMs + boundedBackoffMs[0];
  return {
    result: {schemaVersion: 1, stationId, status: "RETRY_SCHEDULED", selectedCandidateId: null, retryAtMs, trace},
    health: nextHealth
  };
}
