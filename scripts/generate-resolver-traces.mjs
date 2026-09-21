import {readFile, writeFile} from "node:fs/promises";
import {createHealthState, resolveStation} from "../resolver/resolver.mjs";
import {createDeterministicClock} from "../resolver/deterministic-clock.mjs";

const root = new URL("../", import.meta.url);
const readJson = async path => JSON.parse(await readFile(new URL(path, root), "utf8"));
const [catalog, fixture] = await Promise.all([
  readJson("ui-contract/catalog/station-catalog.v1.json"),
  readJson("ui-contract/fixtures/resolver-scenarios.json")
]);
const clock = createDeterministicClock(fixture.startMs);
const traces = fixture.scenarios.map(scenario => {
  const health = createHealthState(catalog);
  const {result} = resolveStation({
    catalog,
    stationId: scenario.stationId,
    health,
    nowMs: clock.now(),
    outcomes: scenario.outcomes,
    lastKnownGood: scenario.lastKnownGood,
    supportedCapabilities: fixture.supportedCapabilities
  });
  if (result.status !== scenario.expectedStatus) throw new Error(`${scenario.id}: expected ${scenario.expectedStatus}, received ${result.status}`);
  clock.advance(1_000);
  return {scenarioId: scenario.id, ...result};
});
const generated = {schemaVersion: 1, generatedFrom: "resolver-scenarios.json", traces};
const output = `${JSON.stringify(generated, null, 2)}\n`;
const outputUrl = new URL("ui-contract/fixtures/resolver-traces.json", root);
if (process.argv.includes("--print")) process.stdout.write(output);
else if (process.argv.includes("--write")) {
  await writeFile(outputUrl, output);
  process.stdout.write("WROTE ui-contract/fixtures/resolver-traces.json\n");
} else {
  const existing = JSON.parse(await readFile(outputUrl, "utf8"));
  if (JSON.stringify(existing) !== JSON.stringify(generated)) {
    process.stderr.write("DRIFT ui-contract/fixtures/resolver-traces.json; run npm run generate:resolver\n");
    process.exit(1);
  }
  process.stdout.write(`PASS resolver-traces stations=${traces.length}\n`);
}
