#!/usr/bin/env python3

import argparse
import base64
from io import BytesIO
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "third_party/artwork/kiara/pixel-cat.png.b64"
OUTPUT = ROOT / "renderer/generated/kiara_sprite.hpp"
EXPECTED_SIZE = (57, 56)


def quantize(pixel):
    red, green, blue, alpha = pixel
    if alpha < 128:
        return None
    luminance = (red + green + blue) / 3
    return round(luminance * 3 / 255)


def build_header():
    image = Image.open(BytesIO(base64.b64decode(SOURCE.read_text()))).convert("RGBA")
    if image.size != EXPECTED_SIZE:
        raise SystemExit(f"Unexpected Kiara source size: {image.size}")

    runs = []
    for y in range(image.height):
        x = 0
        while x < image.width:
            color = quantize(image.getpixel((x, y)))
            if color is None:
                x += 1
                continue
            end = x + 1
            while end < image.width and quantize(image.getpixel((end, y))) == color:
                end += 1
            runs.append((x, y, end - x, color))
            x = end

    rows = "\n".join(
        f"    KiaraRun{{{x}, {y}, {length}, {color}}},"
        for x, y, length, color in runs
    )
    return f"""#pragma once

#include <array>
#include <cstdint>

namespace open_radio::ui::generated {{

struct KiaraRun {{
  std::uint8_t x;
  std::uint8_t y;
  std::uint8_t length;
  std::uint8_t color;
}};

inline constexpr std::uint8_t kKiaraWidth = {image.width};
inline constexpr std::uint8_t kKiaraHeight = {image.height};
inline constexpr std::array<KiaraRun, {len(runs)}> kKiaraRuns{{{{
{rows}
}}}};

}}
"""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    expected = build_header()
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text() != expected:
            raise SystemExit("Kiara generated sprite is stale")
        print(f"PASS kiara-sprite runs={expected.count('KiaraRun{')}")
        return
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(expected)
    print(f"WROTE {OUTPUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
