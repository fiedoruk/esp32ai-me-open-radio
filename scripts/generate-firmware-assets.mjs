import {createHash} from "node:crypto";
import {mkdir, readFile, writeFile} from "node:fs/promises";

const root = new URL("../", import.meta.url);
const catalogUrl = new URL("ui-contract/catalog/station-catalog.v1.json", root);
const onboardingFiles = ["index.html", "styles.css", "state-machine.mjs", "app.mjs"];
const onboardingContentTypes = {
  "index.html": "Html",
  "styles.css": "Css",
  "state-machine.mjs": "JavaScript",
  "app.mjs": "JavaScript"
};
const stationHeaderUrl = new URL("firmware/generated/station_catalog.hpp", root);
const onboardingHeaderUrl = new URL("firmware/generated/onboarding_assets.hpp", root);
const manifestUrl = new URL("firmware/generated/assets-manifest.json", root);
const checkOnly = process.argv.includes("--check");

const sha256 = value => createHash("sha256").update(value).digest("hex");
const escapeCpp = value => value.replaceAll("\\", "\\\\").replaceAll('"', '\\"');

const catalog = JSON.parse(await readFile(catalogUrl, "utf8"));
const directoryUrl = new URL("ui-contract/catalog/pl-directory.v1.json", root);
const directory = JSON.parse(await readFile(directoryUrl, "utf8"));
// The order 0.2.1 shipped. Frozen data, never regenerated: a cube installed
// before ids existed stored a bare index into that order, and this is the only
// thing that can translate it (firmware/manifests/legacy-station-index.json).
const legacyIndexUrl = new URL("firmware/manifests/legacy-station-index.json", root);
const legacyIndex = JSON.parse(await readFile(legacyIndexUrl, "utf8"));

// Everything the installer can choose from, baked in. The picker runs on the
// device's own access point, where there is no internet by definition, so a
// live catalog is not an option at the moment the choice is made. Every entry
// here answered our own probe on directory.probedAt.
const directoryEndpointRows = [];
// Class C holds realtime but recovers slowest and stays out of the shipped
// picker until proven on a device; classes A and B carry either wide measured
// headroom or the exact delivery class Radio ESKA proved clean on 0.1.
//
// Then a SECOND filter, on a different axis. Tier answers "does it deliver";
// it cannot answer "can a person tell what this is". Three entries pass the
// first and fail the second — they are called 'radio', 'rp' and 'rmf'. Held
// back by id from a manifest rather than by a rule invented here, so the
// reason travels with the exclusion and check:station-count reads the same
// list instead of a second copy that can drift away from this one.
const exclusionsUrl = new URL("firmware/manifests/directory-exclusions.json", root);
const exclusions = JSON.parse(await readFile(exclusionsUrl, "utf8"));

// Uniqueness is checked on the WHOLE catalog and BEFORE anything is filtered.
// Checking it after exclusion asks the question of a set the exclusion already
// emptied: a second station colliding onto an excluded id vanishes with it and
// never reaches the uniqueness test, so the collision removes an innocent
// station and reports nothing.
const allDuplicateIds = directory.stations
  .map(station => station.id)
  .filter((id, index, all) => all.indexOf(id) !== index);
if (allDuplicateIds.length > 0) {
  throw new Error(`directory ids must be unique across the whole catalog; ` +
    `duplicated: ${[...new Set(allDuplicateIds)].join(", ")}`);
}

// Bound to the (id, name) PAIR, and to exactly one row. Ids are DERIVED FROM
// THE NAME in build-station-directory.mjs, so they are unique but not stable:
// rename the junk entry and its id moves, while an unrelated new station can
// derive the id that was just vacated. Matching on id alone then withholds the
// newcomer and ships the junk, with the count unchanged and every gate green —
// demonstrated by execution during the 2026-08-01 review, not hypothesised.
// The manifest already carried `name`; it was being ignored.
// The streams, hashed, are what an entry actually IS. Name and id are both
// movable — id is derived from the name — so a (id, name) match still only
// proves that SOME row carries the pair, not that it is the same station:
// rename the junk entry and let an unrelated station derive the freed id, and
// the exclusion withholds the newcomer while the junk ships under its new
// name, count unchanged, gates green. Endpoints do not move in a rename.
const endpointsHash = station => createHash("sha256")
  .update(JSON.stringify(station.endpoints.map(endpoint => endpoint.url).sort()))
  .digest("hex");
const excludedIds = new Set();
for (const entry of exclusions.entries) {
  if (typeof entry.endpointsHash !== "string" || entry.endpointsHash.length !== 64) {
    throw new Error(`directory-exclusions.json entry ${JSON.stringify(entry.id)} has no ` +
      `endpointsHash. An exclusion pinned only to a name can be moved off its station.`);
  }
  const matches = directory.stations.filter(station =>
    station.id === entry.id && station.name === entry.name &&
    endpointsHash(station) === entry.endpointsHash);
  if (matches.length !== 1) {
    const byId = directory.stations.filter(station => station.id === entry.id);
    throw new Error(
      `directory-exclusions.json entry ${JSON.stringify(entry.id)} / ` +
      `${JSON.stringify(entry.name)} matched ${matches.length} stations, expected exactly 1. ` +
      (byId.length > 0
        ? `That id now belongs to ${JSON.stringify(byId[0].name)} with streams ` +
          `${endpointsHash(byId[0]).slice(0, 12)}… — the catalog moved and this exclusion ` +
          `would withhold the wrong station while shipping the one it names.`
        : `No station carries that id at all; the catalog moved under the manifest.`) +
      ` Update the entry deliberately — do not relax this to a name-only match.`);
  }
  excludedIds.add(entry.id);
}
const shippedStations = directory.stations.filter(
  station => station.tier !== "C" && !excludedIds.has(station.id));

// Defence in depth, after the filter: whatever the manifest matched, no station
// carrying an excluded entry's STREAMS may ship. Renaming the junk is exactly
// how it would come back, and a rename leaves the streams untouched.
const forbiddenStreams = new Map(
  exclusions.entries.map(entry => [entry.endpointsHash, entry.name]));
const smuggled = shippedStations.filter(station => forbiddenStreams.has(endpointsHash(station)));
if (smuggled.length > 0) {
  throw new Error(`withheld streams are shipping under a new name: ` +
    smuggled.map(station => `${JSON.stringify(station.name)} carries the streams of ` +
      `${JSON.stringify(forbiddenStreams.get(endpointsHash(station)))}`).join("; ") +
    `. Renaming an excluded entry does not make it a different station.`);
}
const directoryRows = shippedStations.map(station => {
  const offset = directoryEndpointRows.length;
  for (const endpoint of station.endpoints) {
    directoryEndpointRows.push(`  {"${escapeCpp(endpoint.url)}", ${endpoint.bitrateKbps ?? 128}}`);
  }
  return `  {"${escapeCpp(station.name)}", "${escapeCpp(station.favicon ?? "")}", ${offset}, ${station.endpoints.length}}`;
}).join(",\n");
// Stable identity, in directory order, as a flat array so the resolver can
// point straight at it. A saved tile stores an index AND the id that lived
// there; without this the device has no way to notice that a catalog refresh
// reordered the directory under it (docs/124 T2).
const directoryIdRows = shippedStations
  .map(station => `  "${escapeCpp(station.id)}"`).join(",\n");
const duplicateIds = shippedStations
  .map(station => station.id)
  .filter((id, index, all) => all.indexOf(id) !== index);
if (duplicateIds.length > 0) {
  throw new Error(`directory ids must be unique; duplicated: ${duplicateIds.join(", ")}`);
}
if (shippedStations.some(station => !station.id)) {
  throw new Error("every shipped directory station needs a stable id");
}

// One flat endpoint table plus an offset/count pair per station. A flat array
// keeps every string in .rodata with no pointer indirection per station, and
// the device only ever needs "this station's nth alternate".
const playbackRows = [];
const stationOffsets = [];
for (const station of catalog.stations) {
  stationOffsets.push({offset: playbackRows.length, count: station.playback.length});
  for (const endpoint of station.playback) {
    playbackRows.push(`  {"${escapeCpp(endpoint.url)}", ${endpoint.bitrateKbps}, PlaybackResolver::${endpoint.resolver === "RMFON_POOL" ? "RmfonPool" : "Direct"}}`);
  }
}

const stationRows = catalog.stations.map((station, index) =>
  `  {"${escapeCpp(station.id)}", "${escapeCpp(station.displayName)}", "${escapeCpp(station.favicon ?? "")}", CapabilityClass::${station.capabilityClass === "MP3_ICY" ? "Mp3Icy" : "HlsHeAac"}, ${station.firmwareSupport === "MODEL_READY" ? "true" : "false"}, ${stationOffsets[index].offset}, ${stationOffsets[index].count}}`
).join(",\n");

const stationHeader = `#pragma once

#include <cstddef>
#include <cstdint>

#include "open_radio/firmware_contract.hpp"

namespace open_radio {
namespace generated {

// How the device turns a catalog row into an open socket. Direct endpoints are
// opened as written. RmfonPool endpoints are the pinned plain-HTTP fallback for
// a station whose live edges come from the operator's discovery API, which
// needs TLS and therefore only works while the Bluetooth stack is still down.
enum class PlaybackResolver : std::uint8_t { Direct = 0, RmfonPool = 1 };

struct PlaybackEndpoint {
  const char* url;
  std::uint16_t bitrateKbps;
  PlaybackResolver resolver;
};

static constexpr PlaybackEndpoint kPlaybackEndpoints[] = {
${playbackRows.join(",\n")}
};

static constexpr std::size_t kPlaybackEndpointCount = sizeof(kPlaybackEndpoints) / sizeof(kPlaybackEndpoints[0]);

struct StationRecord {
  const char* id;
  const char* displayName;
  const char* favicon;
  CapabilityClass capability;
  bool modelReady;
  std::size_t playbackOffset;
  std::size_t playbackCount;
};

static constexpr StationRecord kStations[] = {
${stationRows}
};

static constexpr std::size_t kStationCount = sizeof(kStations) / sizeof(kStations[0]);

struct DirectoryEndpoint {
  const char* url;
  std::uint16_t bitrateKbps;
};

// The freshness FLOOR of the list: no endpoint below was verified earlier than
// this. It used to say "the date every endpoint answered our probe", which
// stopped being true the moment a station was admitted on its own measurement
// without re-probing the other 375 — and the portal shows this date to an
// installer, so it has to be a claim that stays true as the list grows. A floor
// understates freshness, which is the safe direction for a claim about evidence.
static constexpr const char* kDirectoryProbedAt = "${directory.probedAt}";

static constexpr DirectoryEndpoint kDirectoryEndpoints[] = {
${directoryEndpointRows.join(",\n")}
};

// A station picked from the directory gets the same treatment as one we
// curated by hand: several verified edges and a wheel to rotate through them.
// Quality of service must not depend on how a station reached a slot.
struct DirectoryStation {
  const char* name;
  // Official or community icon URL, verified today to answer with a PNG or
  // JPEG the device can decode; empty when the tile keeps its monogram.
  const char* favicon;
  std::size_t endpointOffset;
  std::size_t endpointCount;
};

static constexpr DirectoryStation kDirectory[] = {
${directoryRows}
};

static constexpr std::size_t kDirectoryCount = sizeof(kDirectory) / sizeof(kDirectory[0]);

// Stable ids in directory order. A saved tile records the id it was pointing
// at, so a reordered directory repoints the tile instead of silently handing
// it a different station (open_radio/station_binding.hpp).
static constexpr const char* kDirectoryIds[] = {
${directoryIdRows}
};

static_assert(sizeof(kDirectoryIds) / sizeof(kDirectoryIds[0]) == kDirectoryCount,
              "every directory station needs exactly one stable id");

// Widest id in the shipped directory, so stored-slot buffers size themselves
// from the data instead of carrying a hand-typed number that goes stale.
static constexpr std::size_t kDirectoryIdBytesMax = ${Math.max(...shippedStations.map(station => station.id.length))};

// The directory order ${legacyIndex.release} shipped, frozen. A cube installed before ids
// existed saved a bare index into THIS list, so it is the only thing that can
// say which station that index meant. Migration translates through it and then
// resolves by identity, which is what lets the directory be re-sorted at all:
// without it, adding one popular station shifts every index below it and every
// upgraded cube quietly plays something its owner never chose.
//
// Never regenerated and never re-sorted — it is a fact about a released binary.
static constexpr const char* kLegacyDirectoryIds[] = {
${legacyIndex.ids.map(id => `  "${escapeCpp(id)}"`).join(",\n")}
};

static constexpr std::size_t kLegacyDirectoryCount = sizeof(kLegacyDirectoryIds) / sizeof(kLegacyDirectoryIds[0]);

static_assert(kLegacyDirectoryCount == ${legacyIndex.count},
              "the pinned ${legacyIndex.release} index map changed size; an installed cube's saved "
              "indices point into the old one, so this is a stored-format change");

}
}
`;

const onboardingSources = {};
for (const file of onboardingFiles) {
  onboardingSources[file] = await readFile(new URL(`network-onboarding/${file}`, root), "utf8");
}

const delimiter = "OPEN_RADIO_ASSET";
const onboardingRows = onboardingFiles.map(file => {
  const identifier = file.replaceAll(/[^a-zA-Z0-9]/g, "_");
  return `static const char k_${identifier}[] = R\"${delimiter}(${onboardingSources[file]})${delimiter}\";`;
}).join("\n\n");

const onboardingTableRows = onboardingFiles.map(file => {
  const identifier = file.replaceAll(/[^a-zA-Z0-9]/g, "_");
  return `  {"/network-onboarding/${escapeCpp(file)}", ContentType::${onboardingContentTypes[file]}, k_${identifier}, sizeof(k_${identifier}) - 1}`;
}).join(",\n");

const onboardingHeader = `#pragma once

#include <cstddef>

#include "open_radio/service_contracts.hpp"

namespace open_radio {
namespace generated {

${onboardingRows}

struct OnboardingAsset {
  const char* path;
  ContentType contentType;
  const char* content;
  std::size_t size;
};

static constexpr OnboardingAsset kOnboardingAssets[] = {
${onboardingTableRows}
};

static constexpr std::size_t kOnboardingAssetCount =
    sizeof(kOnboardingAssets) / sizeof(kOnboardingAssets[0]);

}
}
`;

const manifest = {
  schemaVersion: 1,
  generator: "scripts/generate-firmware-assets.mjs",
  stationCount: catalog.stations.length,
  mp3Ready: catalog.stations.filter(station => station.capabilityClass === "MP3_ICY").length,
  hlsDecoderPending: catalog.stations.filter(station => station.capabilityClass === "HLS_HE_AAC").length,
  // EVERY file this script reads, not the ones that came to mind. The manifest
  // is the provenance record for two compiled headers; an input missing here
  // is an input whose change leaves no trace, and the header still moves. Two
  // were missing until 2026-08-01 — the legacy index and the exclusions —
  // which meant a directory entry could be withheld with nothing recording why
  // the generated catalogue shrank.
  inputs: {
    "ui-contract/catalog/station-catalog.v1.json": sha256(await readFile(catalogUrl)),
    "ui-contract/catalog/pl-directory.v1.json": sha256(await readFile(directoryUrl)),
    "firmware/manifests/legacy-station-index.json": sha256(await readFile(legacyIndexUrl)),
    "firmware/manifests/directory-exclusions.json": sha256(await readFile(exclusionsUrl)),
    ...Object.fromEntries(onboardingFiles.map(file => [`network-onboarding/${file}`, sha256(onboardingSources[file])]))
  },
  outputs: {
    "firmware/generated/station_catalog.hpp": sha256(stationHeader),
    "firmware/generated/onboarding_assets.hpp": sha256(onboardingHeader)
  }
};

async function assertCurrent(url, expected) {
  const actual = await readFile(url, "utf8").catch(() => null);
  if (actual !== expected) throw new Error(`generated file is stale: ${url.pathname}`);
}

if (checkOnly) {
  await assertCurrent(stationHeaderUrl, stationHeader);
  await assertCurrent(onboardingHeaderUrl, onboardingHeader);
  await assertCurrent(manifestUrl, `${JSON.stringify(manifest, null, 2)}\n`);
  console.log(`PASS firmware-assets stations=${manifest.stationCount} onboarding=${onboardingFiles.length}`);
} else {
  await mkdir(new URL("firmware/generated/", root), {recursive: true});
  await writeFile(stationHeaderUrl, stationHeader);
  await writeFile(onboardingHeaderUrl, onboardingHeader);
  await writeFile(manifestUrl, `${JSON.stringify(manifest, null, 2)}\n`);
  console.log(`WROTE firmware-assets stations=${manifest.stationCount} onboarding=${onboardingFiles.length}`);
}
