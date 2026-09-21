import {createHash} from "node:crypto";
import {mkdir, readFile, writeFile} from "node:fs/promises";

import {runIngressTrace} from "../runtime/ingress-replay.mjs";
import {runtimeLimits} from "../runtime/generated/runtime-limits.mjs";

const root = new URL("../", import.meta.url);
const checkOnly = process.argv.includes("--check");
const contractPath = "runtime/ingress-contract.json";
const contractBytes = await readFile(new URL(contractPath, root));
const contract = JSON.parse(contractBytes);
const sha256 = value => createHash("sha256").update(value).digest("hex");
const cppString = value => `"${value.replaceAll("\\", "\\\\").replaceAll('"', '\\"')}"`;
const cppOptionalString = value => value ? cppString(value) : "nullptr";
const enumName = value => value.split("_").map(part => part[0] + part.slice(1).toLowerCase()).join("");

function xorshift(value) {
  let next = value >>> 0;
  next ^= next << 13;
  next ^= next >>> 17;
  next ^= next << 5;
  return next >>> 0;
}

function shuffled(values, seed) {
  const result = values.map(value => ({...value}));
  let random = seed >>> 0;
  for (let index = result.length - 1; index > 0; --index) {
    random = xorshift(random);
    const selected = random % (index + 1);
    [result[index], result[selected]] = [result[selected], result[index]];
  }
  return result;
}

const permutations = contract.permutation.seeds.map(seed => ({
  id: `${contract.permutation.idPrefix}-${seed}`,
  seed,
  convergenceGroup: contract.permutation.convergenceGroup,
  facts: [...shuffled(contract.permutation.facts, seed), ...contract.permutation.settleFacts]
}));
const traces = [...contract.scenarios, ...permutations].map(trace => ({
  ...trace,
  expected: runIngressTrace({trace, ingressLimits: contract.limits, runtimeLimits})
}));

if (new Set(traces.map(trace => trace.id)).size !== traces.length) {
  throw new Error("ingress trace ids must be unique");
}

const limitsMjs = `export const ingressLimits = Object.freeze(${JSON.stringify(contract.limits, null, 2)});\n`;
const fixture = `${JSON.stringify({
  schemaVersion: 1,
  generatedFrom: contractPath,
  limits: contract.limits,
  traces
}, null, 2)}\n`;

const limitsHeader = `#pragma once

#include <cstddef>

namespace open_radio {
namespace generated {

inline constexpr std::size_t kRuntimeIngressMailboxCapacity = ${contract.limits.mailboxCapacity};
inline constexpr std::size_t kRuntimeIngressProducerCount = ${contract.limits.producerCount};
inline constexpr std::size_t kRuntimeIngressRawTickBits = ${contract.limits.rawTickBits};
inline constexpr std::size_t kRuntimeIngressMaximumDrainPerPass = ${contract.limits.maximumDrainPerPass};

}
}
`;

const factArrays = traces.map((trace, index) => `inline constexpr std::array<GoldenIngressFact, ${trace.facts.length}> kRuntimeIngressFacts${index}{{\n${trace.facts.map(fact =>
  `  {RuntimeProducer::${enumName(fact.producer)}, ${fact.epoch}U, ${fact.producerSequence}U, ${fact.rawTick}U, RuntimeEventKind::${enumName(fact.type)}}`
).join(",\n")}\n}};`).join("\n\n");
const expectedRow = expected => `{${`RuntimeState::${enumName(expected.finalState)}`}, ${expected.finalOutput === "BT" ? "OutputKind::Bluetooth" : "OutputKind::LocalSpeaker"}, ${expected.acceptedFacts}U, ${expected.deliveredFacts}U, ${expected.mailboxOverflows}U, ${expected.invalidFacts}U, ${expected.duplicateFacts}U, ${expected.staleFacts}U, ${expected.staleEpochs}U, ${expected.backwardTicks}U, ${expected.rollovers}U, ${expected.maximumMailboxDepth}U, ${expected.deliveryRejected}U, ${expected.runtimeProcessedEvents}U, 0x${expected.stateHash}ULL}`;
const traceRows = traces.map((trace, index) =>
  `  {${cppString(trace.id)}, ${trace.seed ?? 0}U, ${cppOptionalString(trace.convergenceGroup)}, kRuntimeIngressFacts${index}.data(), kRuntimeIngressFacts${index}.size(), ${expectedRow(trace.expected)}}`
).join(",\n");
const vectorsHeader = `#pragma once

#include <array>

#include "open_radio/runtime_ingress.hpp"

namespace open_radio {
namespace generated {

${factArrays}

inline constexpr std::array<GoldenIngressTrace, ${traces.length}> kRuntimeIngressTraces{{
${traceRows}
}};

}
}
`;

const outputs = new Map([
  ["runtime/generated/ingress-limits.mjs", limitsMjs],
  ["ui-contract/fixtures/callback-traces.json", fixture],
  ["firmware/generated/runtime_ingress_limits.hpp", limitsHeader],
  ["firmware/generated/runtime_ingress_vectors.hpp", vectorsHeader]
]);
const manifest = `${JSON.stringify({
  schemaVersion: 1,
  generator: "scripts/generate-ingress-contract.mjs",
  input: {[contractPath]: sha256(contractBytes)},
  counts: {
    traces: traces.length,
    facts: traces.reduce((sum, trace) => sum + trace.facts.length, 0),
    permutations: permutations.length
  },
  outputs: Object.fromEntries([...outputs].map(([path, value]) => [path, sha256(value)]))
}, null, 2)}\n`;
outputs.set("firmware/generated/runtime-ingress-manifest.json", manifest);

if (checkOnly) {
  for (const [path, expected] of outputs) {
    const current = await readFile(new URL(path, root), "utf8").catch(() => null);
    if (current !== expected) throw new Error(`generated ingress contract is stale: ${path}`);
  }
  console.log(`PASS ingress-contract traces=${traces.length} facts=${traces.reduce((sum, trace) => sum + trace.facts.length, 0)}`);
} else {
  await mkdir(new URL("runtime/generated/", root), {recursive: true});
  await mkdir(new URL("ui-contract/fixtures/", root), {recursive: true});
  await mkdir(new URL("firmware/generated/", root), {recursive: true});
  for (const [path, value] of outputs) await writeFile(new URL(path, root), value);
  console.log(`WROTE ingress-contract traces=${traces.length} facts=${traces.reduce((sum, trace) => sum + trace.facts.length, 0)}`);
}
