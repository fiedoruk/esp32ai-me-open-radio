// Assemble the two artifacts a release ships, from the parts a build produced.
//
// The factory image is the four parts laid at their flash offsets with 0xFF
// everywhere between them — including across the settings partition, which is
// what makes "erases all settings" true rather than merely intended. The update
// artifact is the application alone, byte for byte the same application that
// sits inside the factory image.
//
// This existed as a sequence of commands typed by hand, which is how the wrong
// image reached the device yesterday. It is a script now, and its output is
// checked by scripts/check-merged-image.mjs rather than trusted: an assembler
// and its verifier written together are worth less than an assembler and a
// verifier that were written for different reasons.
//
// Usage:
//   node scripts/assemble-release-images.mjs <build-dir> <boot_app0.bin> <out-dir> <version-slug>

import {readFile, writeFile, mkdir} from "node:fs/promises";
import {createHash} from "node:crypto";
import {argv, exit, stdout} from "node:process";
import {join} from "node:path";

const [, , buildDir, bootAppPath, outDir, slug] = argv;
if (!buildDir || !bootAppPath || !outDir || !slug) {
  process.stderr.write("usage: assemble-release-images.mjs <build-dir> <boot_app0.bin> <out-dir> <version-slug>\n");
  exit(2);
}

// The Core2 layout. These offsets are the flasher's contract; a part written
// one address off is a device that does not boot.
const PARTS = [
  {file: "bootloader.bin", offset: 0x1000, from: "build"},
  {file: "partitions.bin", offset: 0x8000, from: "build"},
  {file: "boot_app0.bin", offset: 0xe000, from: "argument"},
  {file: "firmware.bin", offset: 0x10000, from: "build"}
];

const sha = buffer => createHash("sha256").update(buffer).digest("hex");

const loaded = [];
for (const part of PARTS) {
  const path = part.from === "argument" ? bootAppPath : join(buildDir, part.file);
  const body = await readFile(path);
  loaded.push({...part, body, path});
}

const end = Math.max(...loaded.map(part => part.offset + part.body.length));
// Erased flash reads as 0xFF. Filling with anything else would put bytes into
// the settings partition and make the factory image a liar.
const merged = Buffer.alloc(end, 0xff);
for (const part of loaded) part.body.copy(merged, part.offset);

const application = loaded.find(part => part.file === "firmware.bin").body;

await mkdir(outDir, {recursive: true});
const factoryName = `open-radio-${slug}.bin`;
const updateName = `open-radio-${slug}-update.bin`;
await writeFile(join(outDir, factoryName), merged);
await writeFile(join(outDir, updateName), application);

// The parts travel with the artifacts so a record can name them and a verifier
// can re-derive the merged image without this script.
for (const part of loaded) await writeFile(join(outDir, part.file), part.body);

// Checksum sidecars in the form a downloader can actually use: a bare name, so
// `shasum -a 256 -c` works in the directory the files land in.
for (const name of [factoryName, updateName]) {
  const body = await readFile(join(outDir, name));
  await writeFile(join(outDir, `${name}.sha256`), `${sha(body)}  ${name}\n`);
}

stdout.write(`ASSEMBLED ${factoryName} bytes=${merged.length} sha256=${sha(merged)}\n`);
stdout.write(`ASSEMBLED ${updateName} bytes=${application.length} sha256=${sha(application)}\n`);
for (const part of loaded) {
  stdout.write(`  part ${part.file}@0x${part.offset.toString(16)} bytes=${part.body.length} sha256=${sha(part.body).slice(0, 16)}\n`);
}
