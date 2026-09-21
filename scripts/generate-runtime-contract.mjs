import {createHash} from "node:crypto";
import {mkdir, readFile, writeFile} from "node:fs/promises";

import {buildSoakEvents, runVirtualSoak} from "../runtime/soak.mjs";

const root = new URL("../", import.meta.url);
const checkOnly = process.argv.includes("--check");
const contractPath = "runtime/runtime-contract.json";
const contractBytes = await readFile(new URL(contractPath, root));
const contract = JSON.parse(contractBytes);
const sha256 = value => createHash("sha256").update(value).digest("hex");
const cppString = value => `"${value.replaceAll("\\", "\\\\").replaceAll('"', '\\"')}"`;
const eventEnum = value => `RuntimeEventKind::${value.split("_").map(part => part[0] + part.slice(1).toLowerCase()).join("")}`;
const stateEnum = value => `RuntimeState::${value.split("_").map(part => part[0] + part.slice(1).toLowerCase()).join("")}`;
const outputEnum = value => `OutputKind::${value === "BT" ? "Bluetooth" : "LocalSpeaker"}`;

const evaluated = contract.soaks.map(scenario => {
  const events = buildSoakEvents(scenario);
  return {...scenario, events, expected: runVirtualSoak({scenario, limits: contract.limits, events})};
});

const limitsMjs = `export const runtimeLimits = Object.freeze(${JSON.stringify(contract.limits, null, 2)});\n`;
const fixture = `${JSON.stringify({
  schemaVersion: 1,
  generatedFrom: contractPath,
  limits: contract.limits,
  soaks: evaluated
}, null, 2)}\n`;

const eventArrays = evaluated.map((scenario, index) => `inline constexpr std::array<GoldenRuntimeEvent, ${scenario.events.length}> kRuntimeEvents${index}{{\n${scenario.events.map(event =>
  `  {${event.minute}, ${eventEnum(event.type)}, ${event.sequence}U}`
).join(",\n")}\n}};`).join("\n\n");

const expectedRow = expected => `{${stateEnum(expected.finalState)}, ${outputEnum(expected.finalOutput)}, ${expected.recoveries}U, ${expected.networkRetries}U, ${expected.streamRetries}U, ${expected.bluetoothRetries}U, ${expected.powerInterruptions}U, ${expected.maximumQueueDepth}U, ${expected.diagnosticsStored}U, ${expected.diagnosticOverwrites}U, ${expected.queueOverflows}U, ${expected.timerOverflows}U, 0x${expected.stateHash}ULL}`;
const vectorRows = evaluated.map((scenario, index) =>
  `  {${cppString(scenario.id)}, ${scenario.durationMinutes}, kRuntimeEvents${index}.data(), kRuntimeEvents${index}.size(), ${expectedRow(scenario.expected)}}`
).join(",\n");

const limitsHeader = `#pragma once

#include <cstddef>
#include <cstdint>

namespace open_radio {
namespace generated {

inline constexpr std::size_t kRuntimeEventQueueCapacity = ${contract.limits.eventQueueCapacity};
inline constexpr std::size_t kRuntimeTimerCapacity = ${contract.limits.timerCapacity};
inline constexpr std::size_t kRuntimeDiagnosticCapacity = ${contract.limits.diagnosticCapacity};
inline constexpr std::uint64_t kRuntimeMaximumRetryDelayMs = ${contract.limits.maximumRetryDelayMs}ULL;

}
}
`;

const vectorsHeader = `#pragma once

#include <array>

#include "open_radio/runtime_orchestrator.hpp"

namespace open_radio {
namespace generated {

${eventArrays}

inline constexpr std::array<GoldenRuntimeSoakVector, ${evaluated.length}> kRuntimeSoakVectors{{
${vectorRows}
}};

}
}
`;

const outputs = new Map([
  ["runtime/generated/runtime-limits.mjs", limitsMjs],
  ["ui-contract/fixtures/runtime-soaks.json", fixture],
  ["firmware/generated/runtime_limits.hpp", limitsHeader],
  ["firmware/generated/runtime_soak_vectors.hpp", vectorsHeader]
]);
const manifest = `${JSON.stringify({
  schemaVersion: 1,
  generator: "scripts/generate-runtime-contract.mjs",
  input: {[contractPath]: sha256(contractBytes)},
  counts: {
    soaks: evaluated.length,
    events: evaluated.reduce((sum, scenario) => sum + scenario.events.length, 0),
    virtualMinutes: evaluated.reduce((sum, scenario) => sum + scenario.durationMinutes, 0)
  },
  outputs: Object.fromEntries([...outputs].map(([path, value]) => [path, sha256(value)]))
}, null, 2)}\n`;
outputs.set("firmware/generated/runtime-contract-manifest.json", manifest);

if (checkOnly) {
  for (const [path, expected] of outputs) {
    const current = await readFile(new URL(path, root), "utf8").catch(() => null);
    if (current !== expected) throw new Error(`generated runtime contract is stale: ${path}`);
  }
  console.log(`PASS runtime-contract soaks=${evaluated.length} events=${evaluated.reduce((sum, scenario) => sum + scenario.events.length, 0)}`);
} else {
  await mkdir(new URL("runtime/generated/", root), {recursive: true});
  await mkdir(new URL("firmware/generated/", root), {recursive: true});
  for (const [path, value] of outputs) await writeFile(new URL(path, root), value);
  console.log(`WROTE runtime-contract soaks=${evaluated.length} events=${evaluated.reduce((sum, scenario) => sum + scenario.events.length, 0)}`);
}
