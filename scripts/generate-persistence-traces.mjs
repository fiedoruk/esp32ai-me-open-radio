import {readFile, writeFile} from "node:fs/promises";
import {crc32} from "../persistence/checksum.mjs";
import {createCommittedSlot, applyAtomicWrite, selectBootConfig} from "../persistence/persistence.mjs";

const root = new URL("../", import.meta.url);
const readJson = async path => JSON.parse(await readFile(new URL(path, root), "utf8"));
const [fixture, configFixture, catalog] = await Promise.all([
  readJson("ui-contract/fixtures/persistence-scenarios.json"),
  readJson("ui-contract/fixtures/persistence-configs.json"),
  readJson("ui-contract/catalog/station-catalog.v1.json")
]);
const allowedStationIds = catalog.stations.map(station => station.id);
const {currentA, currentB, legacy, future} = configFixture.configs;

function setupStorage(setup, nowMs) {
  const baseline = createCommittedSlot("A", 1, currentA, nowMs - 1000);
  if (setup === "EMPTY") return {slots: {A: null, B: null}};
  if (setup === "BASELINE") return {slots: {A: baseline, B: null}};
  if (setup === "LEGACY_ONLY") return {slots: {A: createCommittedSlot("A", 1, legacy, nowMs - 1000), B: null}};
  if (setup === "FUTURE_ONLY") return {slots: {A: createCommittedSlot("A", 1, future, nowMs - 1000), B: null}};
  if (setup === "CHECKSUM_CORRUPT_NEWEST") {
    const newest = createCommittedSlot("B", 2, currentB, nowMs - 500);
    return {slots: {A: baseline, B: {...newest, checksum: "crc32:00000000"}}};
  }
  if (setup === "PARSE_CORRUPT_NEWEST") {
    const newest = createCommittedSlot("B", 2, currentB, nowMs - 500);
    const payloadText = newest.payloadText.slice(0, Math.max(1, newest.payloadText.length - 7));
    return {slots: {A: baseline, B: {...newest, payloadText, checksum: crc32(payloadText)}}};
  }
  throw new Error(`unknown persistence setup: ${setup}`);
}

const traces = fixture.scenarios.map((scenario, index) => {
  const nowMs = fixture.startMs + index * 1000;
  let storage = setupStorage(scenario.setup, nowMs);
  let write = null;
  if (scenario.writePhase) {
    const applied = applyAtomicWrite(storage, currentB, scenario.writePhase, nowMs, allowedStationIds);
    storage = applied.storage;
    write = applied.write;
  }
  const selection = selectBootConfig(storage, allowedStationIds).result;
  if (selection.status !== scenario.expectedStatus || selection.selectedSlotId !== scenario.expectedSlot || selection.selectedGeneration !== scenario.expectedGeneration) {
    throw new Error(`${scenario.id}: unexpected boot selection`);
  }
  if ((scenario.expectedMigratedFrom ?? null) !== selection.migratedFromVersion) throw new Error(`${scenario.id}: unexpected migration result`);
  return {scenarioId: scenario.id, write, boot: selection};
});

const generated = {schemaVersion: 1, generatedFrom: "persistence-scenarios.json", traces};
const output = `${JSON.stringify(generated, null, 2)}\n`;
const outputUrl = new URL("ui-contract/fixtures/persistence-traces.json", root);
if (process.argv.includes("--print")) process.stdout.write(output);
else if (process.argv.includes("--write")) {
  await writeFile(outputUrl, output);
  process.stdout.write("WROTE ui-contract/fixtures/persistence-traces.json\n");
} else {
  const existing = JSON.parse(await readFile(outputUrl, "utf8"));
  if (JSON.stringify(existing) !== JSON.stringify(generated)) {
    process.stderr.write("DRIFT ui-contract/fixtures/persistence-traces.json; run npm run generate:persistence\n");
    process.exit(1);
  }
  process.stdout.write(`PASS persistence-traces scenarios=${traces.length}\n`);
}
