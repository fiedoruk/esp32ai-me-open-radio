import {readFile, writeFile} from "node:fs/promises";
import {selectKnownNetwork} from "../network/network-selector.mjs";

const root = new URL("../", import.meta.url);
const readJson = async path => JSON.parse(await readFile(new URL(path, root), "utf8"));
const [profileFixture, scenarioFixture] = await Promise.all([
  readJson("ui-contract/fixtures/network-profiles.json"),
  readJson("ui-contract/fixtures/network-scenarios.json")
]);

function profilesFor(state, nowMs) {
  const profiles = structuredClone(profileFixture.profiles);
  const home = profiles.find(profile => profile.ref === "wifi:home");
  if (state === "HOME_QUARANTINED") {
    home.healthScore = 45;
    home.consecutiveFailures = 2;
    home.quarantineUntilMs = nowMs + 30_000;
  } else if (state === "HOME_RECOVERED") {
    home.healthScore = 100;
    home.consecutiveFailures = 0;
    home.quarantineUntilMs = 0;
    home.lastSuccessMs = nowMs - 1_000;
  } else if (state !== "BASE") throw new Error(`unknown profile state: ${state}`);
  return profiles;
}

const traces = scenarioFixture.scenarios.map((scenario, index) => {
  const nowMs = scenarioFixture.startMs + index * 1_000;
  const selection = selectKnownNetwork({profiles: profilesFor(scenario.profileState, nowMs), scans: scenario.scans, nowMs});
  if (selection.status !== scenario.expectedStatus || selection.selectedProfileRef !== scenario.expectedProfileRef) throw new Error(`${scenario.id}: unexpected selection`);
  return {scenarioId: scenario.id, ...selection};
});
const generated = {schemaVersion: 1, generatedFrom: "network-scenarios.json", traces};
const output = `${JSON.stringify(generated, null, 2)}\n`;
const outputUrl = new URL("ui-contract/fixtures/network-traces.json", root);
if (process.argv.includes("--print")) process.stdout.write(output);
else if (process.argv.includes("--write")) {
  await writeFile(outputUrl, output);
  process.stdout.write("WROTE ui-contract/fixtures/network-traces.json\n");
} else {
  const existing = JSON.parse(await readFile(outputUrl, "utf8"));
  if (JSON.stringify(existing) !== JSON.stringify(generated)) {
    process.stderr.write("DRIFT ui-contract/fixtures/network-traces.json; run npm run generate:network\n");
    process.exit(1);
  }
  process.stdout.write(`PASS network-traces scenarios=${traces.length}\n`);
}
