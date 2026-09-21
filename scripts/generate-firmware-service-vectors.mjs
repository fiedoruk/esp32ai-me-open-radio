import {createHash} from "node:crypto";
import {mkdir, readFile, writeFile} from "node:fs/promises";

const root = new URL("../", import.meta.url);
const outputUrl = new URL("firmware/generated/service_golden_vectors.hpp", root);
const manifestUrl = new URL("firmware/generated/service-vectors-manifest.json", root);
const checkOnly = process.argv.includes("--check");
const readJson = async path => JSON.parse(await readFile(new URL(path, root), "utf8"));
const sha256 = value => createHash("sha256").update(value).digest("hex");
const cpp = value => `"${value.replaceAll("\\", "\\\\").replaceAll('"', '\\"')}"`;

const [networkScenarios, networkTraces, persistenceScenarios, persistenceTraces,
  resolverScenarios, resolverTraces, catalog] = await Promise.all([
  readJson("ui-contract/fixtures/network-scenarios.json"),
  readJson("ui-contract/fixtures/network-traces.json"),
  readJson("ui-contract/fixtures/persistence-scenarios.json"),
  readJson("ui-contract/fixtures/persistence-traces.json"),
  readJson("ui-contract/fixtures/resolver-scenarios.json"),
  readJson("ui-contract/fixtures/resolver-traces.json"),
  readJson("ui-contract/catalog/station-catalog.v1.json")
]);

const enumValue = (prefix, value, mapping = {}) => `${prefix}::${mapping[value] ?? value.split("_").map(part => part[0] + part.slice(1).toLowerCase()).join("")}`;
const profileRef = value => enumValue("NetworkProfileRef", value ?? "NONE", {
  "wifi:home": "Home", "wifi:travel": "Travel", "wifi:legacy-home": "LegacyHome",
  "wifi:pending": "Pending", NONE: "None"
});
const security = value => enumValue("NetworkSecurity", value, {
  OPEN: "Open", WPA2_PSK: "Wpa2Psk", WPA3_SAE: "Wpa3Sae", UNKNOWN: "Unknown"
});
const profileState = value => enumValue("NetworkProfileState", value, {
  BASE: "Base", HOME_QUARANTINED: "HomeQuarantined", HOME_RECOVERED: "HomeRecovered"
});
const networkStatus = value => enumValue("NetworkSelectionStatus", value, {
  SELECTED: "Selected", PROMPT_REQUIRED: "PromptRequired", RETRY_SCHEDULED: "RetryScheduled"
});
const persistenceSetup = value => enumValue("PersistenceSetup", value, {
  EMPTY: "Empty", BASELINE: "Baseline", LEGACY_ONLY: "LegacyOnly", FUTURE_ONLY: "FutureOnly",
  CHECKSUM_CORRUPT_NEWEST: "ChecksumCorruptNewest", PARSE_CORRUPT_NEWEST: "ParseCorruptNewest"
});
const writePhase = value => enumValue("WritePhase", value ?? "NONE", {
  NONE: "None", BEFORE_WRITE: "BeforeWrite", DURING_PAYLOAD: "DuringPayload",
  BEFORE_COMMIT_MARKER: "BeforeCommitMarker", AFTER_COMMIT: "AfterCommit"
});
const storageStatus = value => enumValue("StorageStatus", value, {BOOTABLE: "Bootable", SAFE_MODE: "SafeMode"});
const slotId = value => enumValue("SlotId", value ?? "NONE", {A: "A", B: "B", NONE: "None"});
const candidateOutcome = value => enumValue("CandidateOutcome", value ?? "INVALID", {
  SUCCESS: "Success", TIMEOUT: "Timeout", HTTP_404: "Http404", STALE: "Stale",
  PARSE_ERROR: "ParseError", INVALID: "Invalid"
});
const resolverStatus = value => enumValue("ResolverStatus", value, {
  PLAYING: "Playing", RETRY_SCHEDULED: "RetryScheduled", UNSUPPORTED: "Unsupported"
});

const networkRows = networkScenarios.scenarios.map((scenario, index) => {
  const trace = networkTraces.traces.find(item => item.scenarioId === scenario.id);
  const scans = [...scenario.scans, ...Array(Math.max(0, 2 - scenario.scans.length)).fill(null)]
    .slice(0, 2)
    .map(scan => scan
      ? `{${profileRef(scan.profileRef)}, ${security(scan.security)}, ${scan.rssi}, ${scan.reachable}, ${scan.captivePortal}}`
      : `{}`)
    .join(", ");
  return `  {${cpp(scenario.id)}, ${profileState(scenario.profileState)}, ${networkScenarios.startMs + index * 1000}ULL, {{${scans}}}, ${scenario.scans.length}, ${networkStatus(trace.status)}, ${profileRef(trace.selectedProfileRef)}, ${trace.retryAtMs ?? 0}ULL}`;
}).join(",\n");

const persistenceRows = persistenceScenarios.scenarios.map(scenario => {
  const trace = persistenceTraces.traces.find(item => item.scenarioId === scenario.id);
  return `  {${cpp(scenario.id)}, ${persistenceSetup(scenario.setup)}, ${writePhase(scenario.writePhase)}, ${storageStatus(trace.boot.status)}, ${slotId(trace.boot.selectedSlotId)}, ${trace.boot.selectedGeneration ?? 0}, ${trace.boot.migratedFromVersion ?? 0}}`;
}).join(",\n");

const resolverRows = resolverScenarios.scenarios.map(scenario => {
  const trace = resolverTraces.traces.find(item => item.scenarioId === scenario.id);
  const station = catalog.stations.find(item => item.id === scenario.stationId);
  const primary = station.candidates.find(item => item.role === "PRIMARY");
  const primaryOutcome = scenario.outcomes[primary.id] ?? "TIMEOUT";
  const selected = trace.selectedCandidateId?.startsWith("lkg:")
    ? "ResolverSelection::LastKnownGood"
    : trace.selectedCandidateId
      ? "ResolverSelection::Primary"
      : "ResolverSelection::None";
  const firstAt = trace.trace[0]?.atMs ?? resolverScenarios.startMs;
  return `  {${cpp(scenario.id)}, {CapabilityClass::${station.capabilityClass === "MP3_ICY" ? "Mp3Icy" : "HlsHeAac"}, ${candidateOutcome(primaryOutcome)}, ${primary.deviceFeasibility === "REFERENCE_ONLY"}, ${Boolean(scenario.lastKnownGood)}, ${candidateOutcome(scenario.outcomes.LAST_KNOWN_GOOD)}, ${firstAt}ULL}, ${resolverStatus(trace.status)}, ${selected}, ${trace.retryAtMs ?? 0}ULL}`;
}).join(",\n");

const header = `#pragma once

#include <array>

#include "open_radio/service_contracts.hpp"

namespace open_radio {
namespace generated {

inline constexpr std::array<GoldenNetworkVector, ${networkScenarios.scenarios.length}> kNetworkGoldenVectors{{
${networkRows}
}};

inline constexpr std::array<GoldenPersistenceVector, ${persistenceScenarios.scenarios.length}> kPersistenceGoldenVectors{{
${persistenceRows}
}};

inline constexpr std::array<GoldenResolverVector, ${resolverScenarios.scenarios.length}> kResolverGoldenVectors{{
${resolverRows}
}};

}
}
`;

const inputs = [
  "ui-contract/fixtures/network-scenarios.json", "ui-contract/fixtures/network-traces.json",
  "ui-contract/fixtures/persistence-scenarios.json", "ui-contract/fixtures/persistence-traces.json",
  "ui-contract/fixtures/resolver-scenarios.json", "ui-contract/fixtures/resolver-traces.json",
  "ui-contract/catalog/station-catalog.v1.json"
];
const manifest = {
  schemaVersion: 1,
  generator: "scripts/generate-firmware-service-vectors.mjs",
  counts: {network: networkScenarios.scenarios.length, persistence: persistenceScenarios.scenarios.length, resolver: resolverScenarios.scenarios.length},
  inputs: Object.fromEntries(await Promise.all(inputs.map(async path => [path, sha256(await readFile(new URL(path, root)))]))),
  output: {"firmware/generated/service_golden_vectors.hpp": sha256(header)}
};
const manifestText = `${JSON.stringify(manifest, null, 2)}\n`;

if (checkOnly) {
  const currentHeader = await readFile(outputUrl, "utf8").catch(() => null);
  const currentManifest = await readFile(manifestUrl, "utf8").catch(() => null);
  if (currentHeader !== header || currentManifest !== manifestText) throw new Error("generated firmware service vectors are stale");
  console.log(`PASS firmware-service-vectors network=${manifest.counts.network} persistence=${manifest.counts.persistence} resolver=${manifest.counts.resolver}`);
} else {
  await mkdir(new URL("firmware/generated/", root), {recursive: true});
  await writeFile(outputUrl, header);
  await writeFile(manifestUrl, manifestText);
  console.log(`WROTE firmware-service-vectors network=${manifest.counts.network} persistence=${manifest.counts.persistence} resolver=${manifest.counts.resolver}`);
}
