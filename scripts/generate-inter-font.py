#!/usr/bin/env python3

import argparse
import hashlib
from pathlib import Path

import PIL
from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
FONT_PATH = ROOT / "third_party/fonts/inter/InterVariable.ttf"
OUTPUT_PATH = ROOT / "renderer/generated/inter_font.hpp"
EXPECTED_SHA256 = "4989b125924991b90d05b2d16e0e388c48f7d5bb8b30539bbf9c755278d0ccaf"
EXPECTED_PILLOW_VERSION = "12.2.0"
EXPECTED_OUTPUT_SHA256 = "c246360abed1535f23bad3ac877b4458e3f5cb59d6bda16ef75a3a4ec1e82ba4"
CHARACTERS = (
    "".join(chr(codepoint) for codepoint in range(32, 127))
    + "".join(chr(codepoint) for codepoint in range(160, 384))
    + "–—…’‚“”„•€↔"
)
FACES = (("Caption", 12), ("Body", 15), ("Title", 21), ("Display", 32))
WEIGHT = 600


def pack_alpha(image):
    values = [min(15, (value + 8) // 17) for value in image.tobytes()]
    packed = []
    for index in range(0, len(values), 2):
        high = values[index]
        low = values[index + 1] if index + 1 < len(values) else 0
        packed.append((high << 4) | low)
    return packed


def render_face(name, size):
    font = ImageFont.truetype(str(FONT_PATH), size)
    font.set_variation_by_axes([max(14, min(32, size)), WEIGHT])
    ascent, descent = font.getmetrics()
    glyphs = []
    bitmap = []
    for character in sorted(dict.fromkeys(CHARACTERS), key=ord):
        codepoint = ord(character)
        left, top, right, bottom = font.getbbox(character, anchor="ls")
        width = max(0, right - left)
        height = max(0, bottom - top)
        offset = len(bitmap)
        if width and height:
            image = Image.new("L", (width, height), 0)
            ImageDraw.Draw(image).text((-left, -top), character, font=font, fill=255, anchor="ls")
            bitmap.extend(pack_alpha(image))
        advance = max(1, round(font.getlength(character)))
        glyphs.append((codepoint, offset, width, height, left, top, advance))

    glyph_rows = "\n".join(
        f"    {{{codepoint}U, {offset}U, {width}, {height}, {left}, {top}, {advance}}},"
        for codepoint, offset, width, height, left, top, advance in glyphs
    )
    bitmap_rows = []
    for index in range(0, len(bitmap), 16):
        bitmap_rows.append("    " + ", ".join(f"0x{value:02x}" for value in bitmap[index:index + 16]) + ",")
    return f"""inline constexpr std::array<FontGlyph, {len(glyphs)}> kInter{name}Glyphs{{{{
{glyph_rows}
}}}};

inline constexpr std::array<std::uint8_t, {len(bitmap)}> kInter{name}Bitmap{{{{
{chr(10).join(bitmap_rows)}
}}}};

inline constexpr FontFace kInter{name}{{
    kInter{name}Glyphs.data(), kInter{name}Glyphs.size(),
    kInter{name}Bitmap.data(), kInter{name}Bitmap.size(), {ascent}, {descent}}};
"""


def verify_environment():
    if PIL.__version__ != EXPECTED_PILLOW_VERSION:
        raise SystemExit(
            f"Pillow version mismatch: {PIL.__version__}; expected {EXPECTED_PILLOW_VERSION}"
        )
    digest = hashlib.sha256(FONT_PATH.read_bytes()).hexdigest()
    if digest != EXPECTED_SHA256:
        raise SystemExit(f"Inter font checksum mismatch: {digest}")
    return digest


def generated_header():
    digest = verify_environment()
    faces = "\n".join(render_face(name, size) for name, size in FACES)
    return f"""#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "open_radio/renderer.hpp"

namespace open_radio::ui::generated {{

inline constexpr const char* kInterSourceSha256 = "{digest}";
inline constexpr const char* kInterGeneratorPillowVersion = "{EXPECTED_PILLOW_VERSION}";
inline constexpr int kInterWeight = {WEIGHT};

{faces}
constexpr const FontFace& interFace(FontSize size) {{
  switch (size) {{
    case FontSize::caption: return kInterCaption;
    case FontSize::body: return kInterBody;
    case FontSize::title: return kInterTitle;
    case FontSize::display: return kInterDisplay;
  }}
  return kInterBody;
}}

}}
"""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    arguments = parser.parse_args()
    if arguments.check:
        verify_environment()
        actual = OUTPUT_PATH.read_bytes() if OUTPUT_PATH.exists() else b""
        output_digest = hashlib.sha256(actual).hexdigest()
        if output_digest != EXPECTED_OUTPUT_SHA256:
            raise SystemExit(
                "DRIFT renderer/generated/inter_font.hpp; regenerate and review the canonical output hash"
            )
        print(
            f"PASS inter-font glyphs={len(dict.fromkeys(CHARACTERS))} "
            f"faces={len(FACES)} weight={WEIGHT} sha256={output_digest}"
        )
        return
    expected = generated_header()
    OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_PATH.write_text(expected)
    print(f"WROTE {OUTPUT_PATH.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
