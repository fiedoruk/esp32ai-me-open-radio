// Deterministic station logo pack generator (private hardware-lab lane).
//
// Bakes each station's source artwork into fixed-size RGB565 + transparency
// sprites (mip levels 96/48/32 px) using the same run-length + palette
// mechanism as renderer/generated/kiara_sprite.hpp, so the device never
// scales artwork at runtime.
//
// Sources live in assets-local/station-logos/<id>.png (gitignored, NOT
// redistributed). The generated header also lands in that gitignored, rc1-lock
// excluded directory: it is a private hardware-lab artifact, never bundled into
// the public candidate. Same input bytes => identical header.
//
//   node scripts/generate-logo-pack.mjs            # write the pack header
//   node scripts/generate-logo-pack.mjs --check    # fail if it would change
//
// High-quality Lanczos resampling and median-cut quantisation are delegated to
// Python/Pillow (already required by generate-kiara-sprite.py); this script
// owns the deterministic C++ emission.

import {spawnSync} from "node:child_process";
import {existsSync, readFileSync, writeFileSync} from "node:fs";
import {fileURLToPath} from "node:url";

const root = new URL("../", import.meta.url);
const stationsHeader = readFileSync(new URL("renderer/generated/ui_contract.hpp", root), "utf8");
const stationIds = [...stationsHeader.matchAll(/\{"([a-z0-9-]+)", "[^"]*", "[^"]*"/g)].map(m => m[1]);
if (stationIds.length !== 9) {
  throw new Error(`expected 9 station ids from ui_contract.hpp, got ${stationIds.length}`);
}

const sizes = [32, 48, 72, 96, 120];
const sourceDir = new URL("assets-local/station-logos/", root);
const outputUrl = new URL("station_logo_pack.hpp", sourceDir);

// Python: load one source PNG, bake the three mip levels, emit JSON runs.
const python = `
import sys, json
from PIL import Image, ImageChops, ImageDraw

SIZES = [${sizes.join(", ")}]
FIT = sys.argv[2] if len(sys.argv) > 2 else "cover"

def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)

def slot_radius(size):
    # Match the rounded slots the renderer draws: 8 px on the station tile,
    # the badge's fixed 24 px (clamped to a circle on the smallest badge).
    return 8 if size <= 32 else min(24, size // 2)

def bake(img, size):
    if FIT == "cover":
        # Fill the whole slot; scale to the larger edge and centre-crop the rest
        # (parts of very wide wordmarks are cropped away by design).
        scale = max(size / img.width, size / img.height)
        scaled = img.resize((max(1, round(img.width * scale)),
                             max(1, round(img.height * scale))), Image.LANCZOS)
        left = (scaled.width - size) // 2
        top = (scaled.height - size) // 2
        canvas = scaled.crop((left, top, left + size, top + size))
    elif FIT == "pad":
        # pad (owner variant C): the slot is 100% filled with a background
        # sampled from the logo itself (median of opaque border pixels, falling
        # back to white), and the complete wordmark sits centred on top - the
        # app-icon treatment for very wide marks that cover would mutilate.
        px = img.convert("RGBA").load()
        border = []
        for x in range(img.width):
            for y in (0, img.height - 1):
                r, g, b, a = px[x, y]
                if a > 200: border.append((r, g, b))
        for y in range(img.height):
            for x in (0, img.width - 1):
                r, g, b, a = px[x, y]
                if a > 200: border.append((r, g, b))
        if border:
            border.sort()
            bg = border[len(border) // 2]
        else:
            bg = (255, 255, 255)
        margin = max(2, round(size * 0.10))
        content = size - 2 * margin
        logo = img.copy()
        logo.thumbnail((content, content), Image.LANCZOS)
        canvas = Image.new("RGBA", (size, size), bg + (255,))
        canvas.paste(logo, ((size - logo.width) // 2,
                            (size - logo.height) // 2), logo)
    else:
        # contain: keep the whole logo with a small margin (letterboxed).
        margin = max(2, round(size * 0.08))
        content = size - 2 * margin
        logo = img.copy()
        logo.thumbnail((content, content), Image.LANCZOS)
        canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        canvas.paste(logo, ((size - logo.width) // 2, (size - logo.height) // 2))
    # Rounded-slot alpha mask so the sprite matches the badge/tile silhouette.
    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, size - 1, size - 1],
                                           radius=slot_radius(size), fill=255)
    canvas.putalpha(ImageChops.multiply(canvas.getchannel("A"), mask))
    rgb = canvas.convert("RGB")
    # Deterministic median-cut palette (no dithering) keeps the run palette small.
    quant = rgb.quantize(colors=64, method=Image.MEDIANCUT, dither=Image.NONE).convert("RGB")
    alpha = canvas.split()[3].load()
    px = quant.load()
    palette = []
    index_of = {}
    runs = []
    for y in range(size):
        x = 0
        while x < size:
            if alpha[x, y] < 128:
                x += 1
                continue
            r, g, b = px[x, y]
            key = rgb565(r, g, b)
            if key not in index_of:
                index_of[key] = len(palette)
                palette.append(key)
            ci = index_of[key]
            start = x
            x += 1
            while x < size and alpha[x, y] >= 128:
                r2, g2, b2 = px[x, y]
                if rgb565(r2, g2, b2) != key:
                    break
                x += 1
            runs.append([start, y, x - start, ci])
    return {"width": size, "height": size, "palette": palette, "runs": runs}

img = Image.open(sys.argv[1]).convert("RGBA")
print(json.dumps({"sizes": [bake(img, s) for s in SIZES]}))
`;

// Fit mode per station id; default "cover" fills the whole slot. These wide
// wordmarks turn into unrecognisable fragments once centre-cropped to a square
// (e.g. ANTYRADIO -> "TYRAI"), so they keep the full logo via "contain".
const fitOverrides = {
  "antyradio": "pad",
  "meloradio": "pad",
  "zlote-przeboje": "pad",
  "radio-eska": "pad",
};

function bakeStation(id) {
  const png = new URL(`${id}.png`, sourceDir);
  if (!existsSync(png)) return {id, present: false};
  const fit = fitOverrides[id] ?? "cover";
  const result = spawnSync("python3", ["-c", python, fileURLToPath(png), fit], {encoding: "utf8"});
  if (result.status !== 0) throw new Error(result.stderr || `python failed for ${id}`);
  return {id, present: true, ...JSON.parse(result.stdout)};
}

function emit(stations) {
  const lines = [];
  lines.push("#pragma once");
  lines.push("");
  lines.push("// GENERATED by scripts/generate-logo-pack.mjs -- do not edit.");
  lines.push("// Private hardware-lab station artwork. Not bundled into the");
  lines.push("// public candidate; gated behind OPEN_RADIO_HAS_LOGO_PACK.");
  lines.push("");
  lines.push("#include <array>");
  lines.push("#include <cstddef>");
  lines.push("#include <cstdint>");
  lines.push("");
  lines.push("namespace open_radio::ui::generated {");
  lines.push("");
  lines.push("struct LogoRun { std::uint8_t x; std::uint8_t y; std::uint8_t length; std::uint8_t color; };");
  lines.push("struct LogoImage {");
  lines.push("  std::uint8_t width;");
  lines.push("  std::uint8_t height;");
  lines.push("  const std::uint16_t* palette;");
  lines.push("  std::uint16_t palette_size;");
  lines.push("  const LogoRun* runs;");
  lines.push("  std::uint16_t run_count;");
  lines.push("};");
  lines.push(`struct StationLogo { bool present; std::array<LogoImage, ${sizes.length}> mips; };`);
  lines.push("");
  lines.push(`inline constexpr std::array<std::uint8_t, ${sizes.length}> kStationLogoSizes{{${sizes.join(", ")}}};`);
  lines.push("");

  const imageExpr = [];
  for (const station of stations) {
    if (!station.present) {
      imageExpr.push(null);
      continue;
    }
    const mips = station.sizes.map((mip, level) => {
      const tag = `k${cap(station.id)}_${mip.width}`;
      const palette = mip.palette.map(c => `0x${c.toString(16).padStart(4, "0")}`).join(", ");
      const runs = mip.runs.map(r => `{${r[0]},${r[1]},${r[2]},${r[3]}}`).join(", ");
      lines.push(`inline constexpr std::array<std::uint16_t, ${mip.palette.length}> ${tag}_palette{{${palette}}};`);
      lines.push(`inline constexpr std::array<LogoRun, ${mip.runs.length}> ${tag}_runs{{${runs}}};`);
      return {tag, mip};
    });
    lines.push("");
    const mipExpr = mips.map(({tag, mip}) =>
      `LogoImage{${mip.width}, ${mip.height}, ${tag}_palette.data(), ${mip.palette.length}, ${tag}_runs.data(), ${mip.runs.length}}`);
    imageExpr.push(`StationLogo{true, {{${mipExpr.join(", ")}}}}`);
  }

  const empty = "StationLogo{false, {{}}}";
  lines.push(`inline constexpr std::array<StationLogo, 9> kStationLogos{{`);
  for (const expr of imageExpr) lines.push(`    ${expr === null ? empty : expr},`);
  lines.push("}};");
  lines.push("");
  lines.push("}");
  lines.push("");
  return lines.join("\n");
}

function cap(id) {
  return id.replace(/(^|-)([a-z0-9])/g, (_, __, c) => c.toUpperCase());
}

const stations = stationIds.map(bakeStation);
const header = emit(stations);
const check = process.argv.includes("--check");
const present = stations.filter(s => s.present).map(s => s.id);
const missing = stations.filter(s => !s.present).map(s => s.id);

if (check) {
  const current = existsSync(outputUrl) ? readFileSync(outputUrl, "utf8") : null;
  if (current !== header) {
    process.stderr.write("logo pack header is stale; run node scripts/generate-logo-pack.mjs\n");
    process.exit(1);
  }
  process.stdout.write(`PASS logo-pack present=${present.length} missing=${missing.length}\n`);
} else {
  writeFileSync(outputUrl, header);
  process.stdout.write(`PASS logo-pack written present=${present.length} (${present.join(",")}) missing=${missing.length} (${missing.join(",") || "-"})\n`);
}
