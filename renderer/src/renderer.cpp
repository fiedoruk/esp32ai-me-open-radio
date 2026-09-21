#include "open_radio/renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "inter_font.hpp"
#include "icon_masks.hpp"
#include "kiara_sprite.hpp"
#include "ui_contract.hpp"

#if defined(OPEN_RADIO_HAS_LOGO_PACK)
// Private hardware-lab station artwork; never compiled into the public
// candidate (see scripts/generate-logo-pack.mjs).
#include "station_logo_pack.hpp"
#endif

namespace open_radio::ui {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
constexpr int kScreenWidth = 320;

// ONE grid for every home screen, written down so "pixel perfect" is a property
// something can be checked against rather than an opinion. Every margin, every
// gutter and every column on the home screens comes from here:
//
//   8 + 146 + 12 + 146 + 8 = 320
//
// The screens drifted apart before this existed — one used a 10-pixel margin
// and 152-pixel columns, another 8 and 146, and a row of Wi-Fi bars ended 36
// pixels short of the right edge while the bar above it ran the full width.
// Nothing was wrong with any single number and the result still looked crooked,
// because the eye reads the RELATIONSHIP between edges, not the edges.
// Icons are drawn by their INK, not by their bounding box.
//
// Every mask is 24 wide and none of them fills it. kIconTemperature has seven
// empty columns on the left, kIconGauge has two, the droplet another number
// again — so a row of icons all "at x=8" puts its ink at 15, 10 and 12, and the
// row reads as ragged against a bar that genuinely starts at 8. The geometry
// audit reports it as bands beginning at x=15 where the grid says 8.
//
// Measured from the mask itself rather than tabulated, so a regenerated icon
// pack cannot silently reintroduce the drift.
constexpr int iconInkLeft(const IconMask24& icon) {
  int leftmost = 24;
  for (const auto row : icon.rows) {
    if (row == 0U) continue;
    for (int column = 0; column < 24; ++column) {
      // Bit 0 is the LEFTMOST column — the rasteriser walks `bits & (1U <<
      // column)` left to right, so the packing is least significant bit first.
      // The first version of this read it the other way round and produced
      // offsets that were wrong by a couple of pixels in a plausible-looking
      // direction, which the geometry audit caught immediately and an eye would
      // not have.
      if ((row & (1U << static_cast<unsigned>(column))) != 0U) {
        if (column < leftmost) leftmost = column;
        break;
      }
    }
  }
  return leftmost == 24 ? 0 : leftmost;
}

constexpr int iconInkTop(const IconMask24& icon) {
  for (int row = 0; row < 24; ++row) {
    if (icon.rows[static_cast<std::size_t>(row)] != 0U) return row;
  }
  return 0;
}

// Gives a 24-pixel one-bit mask some body.
//
// These masks are flat stamps: one colour, hard edges, no weight. At this size
// that reads as a placeholder rather than an object, and on this panel the
// icons are a third of the information on a readings row.
//
// So the mask is drawn more than once. On a dark canvas the offsets go in a
// dim tint of the ink colour in all four directions, which is a HALO — the
// thing that makes a small bright shape look lit rather than printed. On a
// light canvas the same trick would be invisible, so a single offset in a
// darker tone becomes a drop shadow instead.
//
// The cost is five mask writes instead of one, on shapes of at most 576 pixels.
// Nothing measurable, on a screen that is pushed whole every 133 ms anyway.
inline std::uint16_t dimToward(std::uint16_t colour, std::uint16_t towards, int eighths) {
  const int red = (((colour >> 11) & 0x1F) * (8 - eighths) + ((towards >> 11) & 0x1F) * eighths) / 8;
  const int green = (((colour >> 5) & 0x3F) * (8 - eighths) + ((towards >> 5) & 0x3F) * eighths) / 8;
  const int blue = ((colour & 0x1F) * (8 - eighths) + (towards & 0x1F) * eighths) / 8;
  return static_cast<std::uint16_t>((red << 11) | (green << 5) | blue);
}

// Draws an icon so its INK lands at (x, y), not its bounding box. Use this
// anywhere an icon has to line up with something else; drawIconMask24 remains
// available for the places that position a glyph inside a box of their own.
inline bool drawIconAligned(FramebufferView framebuffer, int x, int y,
                            const IconMask24& icon, std::uint16_t colour) {
  return drawIconMask24(framebuffer, x - iconInkLeft(icon), y - iconInkTop(icon),
                        icon, colour);
}

// An icon with weight. `canvas` decides which treatment applies: a dark canvas
// gets a halo, a light one gets a shadow.
inline bool drawIconLit(FramebufferView framebuffer, int x, int y,
                        const IconMask24& icon, std::uint16_t colour,
                        std::uint16_t canvas) {
  // Luma of the canvas, roughly. Below a third is "dark".
  const int luma = (((canvas >> 11) & 0x1F) * 2 + ((canvas >> 5) & 0x3F) +
                    (canvas & 0x1F) * 2) / 4;
  if (luma < 16) {
    const std::uint16_t halo = dimToward(colour, canvas, 6);
    drawIconAligned(framebuffer, x - 1, y, icon, halo);
    drawIconAligned(framebuffer, x + 1, y, icon, halo);
    drawIconAligned(framebuffer, x, y - 1, icon, halo);
    drawIconAligned(framebuffer, x, y + 1, icon, halo);
  } else {
    drawIconAligned(framebuffer, x + 1, y + 1, icon, dimToward(colour, canvas, 5));
  }
  return drawIconAligned(framebuffer, x, y, icon, colour);
}

constexpr int kHomeMargin = 8;

// The VERTICAL rhythm, which the horizontal grid was missing a partner for.
//
// Until this existed the screen was a set of correct elements at unrelated
// heights: a 64-pixel badge ending at y=69 with a card starting at y=70, so
// they touched; gaps of 0, 8, 8 and 18 between things that were meant to read
// as separate. Every number was defensible on its own and the composition did
// not hold together, which is what "nic ze sobą nie gra" means and no amount of
// nudging a single element fixes.
//
// Seven bands, and they add up exactly:
//
//   8 | 56 header | 76 title | 124 bar | 146 advice | 164 digits | 208 units
//     | 228 humidity .. 240
//
// A band is a place. What goes in it is centred within it, so changing the
// contents of one never moves another.
//
// The bands were re-cut on 2026-07-30 for one reason: the temperature figure
// ran from y=182 to y=224 and the humidity strip began at y=222, so the digits
// were CLIPPED by two pixels of bar. Nothing about the old rhythm was wrong
// except that it had no band for a second large figure, and adding one by
// nudging is how a layout stops being a layout.
// One corner radius for every container on the home screens. There were four —
// 9, 12, 14 and 16 — chosen a shape at a time, and that alone is enough to make
// a screen read as assembled rather than designed: the eye compares corners
// long before it reads anything.
constexpr int kHomeRadius = 12;

constexpr int kBandGap = 12;
constexpr int kBandHeaderTop = 8;
constexpr int kBandHeaderHeight = 56;
constexpr int kBandTitleTop = kBandHeaderTop + kBandHeaderHeight + kBandGap;
constexpr int kBandTitleHeight = 40;
constexpr int kBandBarTop = kBandTitleTop + kBandTitleHeight + 8;
constexpr int kBandBarHeight = 18;
constexpr int kBandAdviceTop = kBandBarTop + kBandBarHeight + 4;
constexpr int kBandAdviceHeight = 16;
constexpr int kBandDigitsTop = kBandAdviceTop + kBandAdviceHeight + 2;
constexpr int kBandDigitsHeight = 42;
constexpr int kBandUnitsTop = kBandDigitsTop + kBandDigitsHeight + 2;
constexpr int kBandUnitsHeight = 12;
// The humidity strip runs to the physical bottom edge, so its "band" ends at
// 240 rather than at the margin. That is the one full bleed on the screen and
// it is deliberate: a strip that stopped short would read as a fifth object
// rather than as the floor of the composition.
constexpr int kBandHumidityTop = kBandUnitsTop + kBandUnitsHeight + 8;
constexpr int kBandHumidityHeight = 12;
static_assert(kBandHumidityTop + kBandHumidityHeight == 240,
              "the bands and their gaps must fill the panel exactly");

// Vertically centres a run of `height` pixels inside a band.
constexpr int centredIn(int bandTop, int bandHeight, int height) {
  return bandTop + (bandHeight - height) / 2;
}

// Ascent of each face, from the generated font. Used to centre text in a band
// rather than guessing an offset per call site — the guesses are what put four
// different gaps on one screen.
constexpr int kAscentCaption = 12;
constexpr int kAscentBody = 15;
constexpr int kAscentTitle = 21;
constexpr int kAscentDisplay = 31;

// Where the ink of a line of body text actually begins inside its cell, and how
// tall its capitals are. Measured from the rendered glyphs, not assumed, and
// asserted by the geometry audit.
//
// This exists because centring the CELL is not centring the text. The cell
// reserves room for descenders whether the line has a "j" in it or not, so a
// title centred by cell sits low when it has none and high when it has them —
// which is exactly the wobble the owner reported on the track title, and which
// no amount of moving the card underneath it can fix. Text is centred on the
// capital block; descenders hang out of it, the way they do on paper.
constexpr int kInkTopBody = 4;
constexpr int kCapBody = 11;
// What drawWrappedText advances between lines: the cell plus its leading.
constexpr int kLineAdvanceBody = kAscentBody + 6;
constexpr int kHomeGutter = 12;
constexpr int kHomeColumn = 146;
constexpr int kHomeColumnTwo = kHomeMargin + kHomeColumn + kHomeGutter;
constexpr int kHomeContent = kScreenWidth - 2 * kHomeMargin;
static_assert(kHomeMargin + kHomeColumn + kHomeGutter + kHomeColumn +
                      kHomeMargin ==
                  kScreenWidth,
              "the two-column home grid must span the panel exactly");

bool requiredPixels(FramebufferView framebuffer, std::size_t& required) {
  if (framebuffer.pixels == nullptr || framebuffer.width <= 0 ||
      framebuffer.height <= 0) {
    return false;
  }
  const auto width = static_cast<std::size_t>(framebuffer.width);
  const auto height = static_cast<std::size_t>(framebuffer.height);
  if (width > std::numeric_limits<std::size_t>::max() / height) return false;
  required = width * height;
  return framebuffer.pixel_count >= required;
}

void putPixel(FramebufferView framebuffer, int x, int y, std::uint16_t color) {
  if (x < 0 || y < 0 || x >= framebuffer.width || y >= framebuffer.height) return;
  framebuffer.pixels[static_cast<std::size_t>(y) *
                         static_cast<std::size_t>(framebuffer.width) +
                     static_cast<std::size_t>(x)] = color;
}

void putPixelAlpha(FramebufferView framebuffer, int x, int y,
                   std::uint16_t foreground, std::uint8_t alpha) {
  if (alpha == 0 || x < 0 || y < 0 || x >= framebuffer.width ||
      y >= framebuffer.height) {
    return;
  }
  auto& background = framebuffer.pixels[
      static_cast<std::size_t>(y) * static_cast<std::size_t>(framebuffer.width) +
      static_cast<std::size_t>(x)];
  if (alpha >= 15) {
    background = foreground;
    return;
  }
  const int inverse = 15 - alpha;
  const int foreground_red = (foreground >> 11U) & 0x1fU;
  const int foreground_green = (foreground >> 5U) & 0x3fU;
  const int foreground_blue = foreground & 0x1fU;
  const int background_red = (background >> 11U) & 0x1fU;
  const int background_green = (background >> 5U) & 0x3fU;
  const int background_blue = background & 0x1fU;
  const auto red = static_cast<std::uint16_t>(
      (foreground_red * alpha + background_red * inverse + 7) / 15);
  const auto green = static_cast<std::uint16_t>(
      (foreground_green * alpha + background_green * inverse + 7) / 15);
  const auto blue = static_cast<std::uint16_t>(
      (foreground_blue * alpha + background_blue * inverse + 7) / 15);
  background = static_cast<std::uint16_t>((red << 11U) | (green << 5U) | blue);
}

// A close cross drawn as two strokes rather than stamped from a mask.
//
// kIconX is a 24-pixel bitmap whose arms are not the same weight, and at this
// size that is visible as a chewed edge on the one control every utility screen
// carries. Two diagonals of equal thickness cannot be asymmetric: the shape is
// generated from the geometry instead of stored as pixels somebody drew once.
inline void drawCross(FramebufferView framebuffer, int centreX, int centreY,
                      int reach, int thickness, std::uint16_t colour) {
  const int half = thickness / 2;
  for (int step = -reach; step <= reach; ++step) {
    for (int spread = -half; spread <= half; ++spread) {
      putPixel(framebuffer, centreX + step + spread, centreY + step, colour);
      putPixel(framebuffer, centreX + step + spread, centreY - step, colour);
    }
  }
}

std::uint32_t decodeUtf8(const unsigned char*& cursor) {
  const std::uint32_t first = *cursor++;
  if (first < 0x80U) return first;
  if ((first & 0xe0U) == 0xc0U && (*cursor & 0xc0U) == 0x80U) {
    return ((first & 0x1fU) << 6U) | (*cursor++ & 0x3fU);
  }
  if ((first & 0xf0U) == 0xe0U && cursor[0] != 0U && cursor[1] != 0U &&
      (cursor[0] & 0xc0U) == 0x80U && (cursor[1] & 0xc0U) == 0x80U) {
    const std::uint32_t second = *cursor++ & 0x3fU;
    const std::uint32_t third = *cursor++ & 0x3fU;
    return ((first & 0x0fU) << 12U) | (second << 6U) | third;
  }
  while ((*cursor & 0xc0U) == 0x80U) ++cursor;
  return '?';
}

const FontGlyph& glyphFor(const FontFace& face, std::uint32_t codepoint) {
  std::size_t left = 0;
  std::size_t right = face.glyph_count;
  while (left < right) {
    const std::size_t middle = left + (right - left) / 2;
    const auto candidate = face.glyphs[middle].codepoint;
    if (candidate < codepoint) left = middle + 1;
    else right = middle;
  }
  if (left < face.glyph_count && face.glyphs[left].codepoint == codepoint) {
    return face.glyphs[left];
  }
  return glyphFor(face, '?');
}

void drawGlyph(FramebufferView framebuffer, int x, int top,
               const FontFace& face, const FontGlyph& glyph,
               std::uint16_t color) {
  if (glyph.width == 0 || glyph.height == 0) return;
  const int glyph_left = x + glyph.x_offset;
  const int glyph_top = top + face.ascent + glyph.y_offset;
  const std::size_t pixels = static_cast<std::size_t>(glyph.width) * glyph.height;
  for (std::size_t index = 0; index < pixels; ++index) {
    const std::size_t byte_index = glyph.bitmap_offset + index / 2;
    if (byte_index >= face.bitmap_size) return;
    const auto packed = face.bitmap[byte_index];
    const std::uint8_t alpha = index % 2 == 0 ? packed >> 4U : packed & 0x0fU;
    const int column = static_cast<int>(index % glyph.width);
    const int row = static_cast<int>(index / glyph.width);
    putPixelAlpha(framebuffer, glyph_left + column, glyph_top + row, color,
                  alpha);
  }
}

int lineHeight(FontSize size) {
  const auto& face = generated::interFace(size);
  return face.ascent + face.descent;
}

void fillCircle(FramebufferView framebuffer, int center_x, int center_y,
                int radius, std::uint16_t color) {
  if (radius <= 0) return;
  const int radius_squared = radius * radius;
  for (int y = -radius; y <= radius; ++y) {
    for (int x = -radius; x <= radius; ++x) {
      if (x * x + y * y <= radius_squared) {
        putPixel(framebuffer, center_x + x, center_y + y, color);
      }
    }
  }
}

// Mixes two colours in eighths, in RGB565 directly.
// Exact linear interpolation, numerator over denominator, per channel with
// rounding. The eight-step
// mixColour this replaces was fine for a halo; across a fifteen-pixel blend
// zone its steps read as bands, and a band in a gradient is exactly the kind
// of defect this panel's size cannot hide.
inline std::uint16_t lerpColour(std::uint16_t from, std::uint16_t to, int numerator,
                                int denominator) {
  if (numerator <= 0 || denominator <= 0) return from;
  if (numerator >= denominator) return to;
  const int fromR = (from >> 11U) & 0x1F, fromG = (from >> 5U) & 0x3F, fromB = from & 0x1F;
  const int toR = (to >> 11U) & 0x1F, toG = (to >> 5U) & 0x3F, toB = to & 0x1F;
  const int r = fromR + ((toR - fromR) * numerator * 2 + denominator) / (denominator * 2);
  const int g = fromG + ((toG - fromG) * numerator * 2 + denominator) / (denominator * 2);
  const int b = fromB + ((toB - fromB) * numerator * 2 + denominator) / (denominator * 2);
  return static_cast<std::uint16_t>((r << 11) | (g << 5) | b);
}


// The CO2 scale as a SPECTRUM rather than a block of one colour.
//
// The first version filled the bar in whichever colour the current band happened
// to be, on a track of theme.surface. Two things were wrong with that. The empty
// track was nearly indistinguishable from the canvas behind it, so the bar had no
// visible length to be a fraction OF; and the colour said only what the band
// already said in words, changing in one jump at a threshold.
//
// Here the colour belongs to the POSITION. Green up to the 1000 mark, then
// through amber, then into red by the top of the scale — so the eye reads where
// on the range a value sits, not merely which of five names it currently has.
// The unreached part is a NEUTRAL track. It carried the same spectrum at a
// quarter strength for a day, on the argument that it showed what was ahead,
// and the owner was right about how that looked: a green-to-olive-to-maroon
// smear reads as dirt, and it stole contrast from the one part of the bar that
// is saying something. A dark track and a saturated fill is the oldest gauge in
// the world for a reason. What is ahead is still marked, by the threshold ticks
// drawn over the top of it.
inline void drawSpectrumBar(FramebufferView framebuffer, Rect bounds, int radius,
                            int fillWidth, std::uint16_t fresh, std::uint16_t warn,
                            std::uint16_t danger, std::uint16_t track,
                            int firstPerThousand = 375, int secondPerThousand = 625) {
  for (int offset = 0; offset < bounds.width; ++offset) {
    const int per_thousand = offset * 1000 / (bounds.width > 0 ? bounds.width : 1);
    const int kFirst = firstPerThousand;
    const int kSecond = secondPerThousand;
    constexpr int kBlend = 25;
    std::uint16_t colour;
    if (per_thousand <= kFirst - kBlend) {
      colour = fresh;
    } else if (per_thousand < kFirst + kBlend) {
      colour = lerpColour(fresh, warn, per_thousand - (kFirst - kBlend), 2 * kBlend);
    } else if (per_thousand <= kSecond - kBlend) {
      colour = warn;
    } else if (per_thousand < kSecond + kBlend) {
      colour = lerpColour(warn, danger, per_thousand - (kSecond - kBlend), 2 * kBlend);
    } else {
      colour = danger;
    }
    const bool reached = offset < fillWidth;
    // A two-pixel breath at the fill boundary instead of a hard cliff: the
    // leading edge of the fill fades into the track the way the capsule's own
    // curve fades into the canvas.
    std::uint16_t painted = reached ? colour : track;
    if (fillWidth > 0 && fillWidth < bounds.width) {
      if (offset == fillWidth) painted = lerpColour(track, colour, 2, 3);
      else if (offset == fillWidth + 1) painted = lerpColour(track, colour, 1, 3);
    }
    // True semicircle caps with anti-aliased arcs. The integer shave alone
    // leaves a staircase the eye reads at arm's length; one coverage-weighted
    // pixel at each boundary turns the step into a curve.
    double exact = 0.0;
    if (radius > 0 && offset < radius) {
      const int across = radius - offset;
      exact = radius - std::sqrt(static_cast<double>(radius * radius - across * across));
    } else if (radius > 0 && offset >= bounds.width - radius) {
      const int across = offset - (bounds.width - 1 - radius);
      const int inside = radius * radius - across * across;
      exact = radius - std::sqrt(static_cast<double>(inside > 0 ? inside : 0));
    }
    const int shave = static_cast<int>(exact + 0.9999);
    for (int row = shave; row < bounds.height - shave; ++row) {
      putPixel(framebuffer, bounds.x + offset, bounds.y + row, painted);
    }
    if (shave > 0) {
      const double coverage = static_cast<double>(shave) - exact;
      const auto alpha = static_cast<std::uint8_t>(coverage * 15.0 + 0.5);
      if (alpha > 0) {
        putPixelAlpha(framebuffer, bounds.x + offset, bounds.y + shave - 1, painted, alpha);
        putPixelAlpha(framebuffer, bounds.x + offset,
                      bounds.y + bounds.height - shave, painted, alpha);
      }
    }
  }
}

// The spectrum endpoints for FILLS, chosen per theme. The text tokens darken
// on the light theme because glyphs need contrast against white — but a
// 12-pixel fill painted with text-grade colours reads as sludge there (owner,
// 2026-07-30: "kolory gorzej na light theme"). Fills therefore get their own
// light-theme trio, saturated and a step brighter, while the dark theme keeps
// its tokens: they were tuned as fills in the first place.
struct SpectrumInk {
  std::uint16_t fresh;
  std::uint16_t warn;
  std::uint16_t danger;
};
inline SpectrumInk spectrumInk(const CompactSnapshot& snapshot,
                               const ThemeTokens& theme) {
  if (snapshot.theme != ThemeMode::light) {
    return {theme.success, theme.warning, theme.danger};
  }
  return {rgb565(16, 168, 104), rgb565(234, 158, 18), rgb565(224, 64, 52)};
}

// A barometer tendency arrow: which way the pressure is going, and how hard.
//
// Pressure is the one reading whose VALUE almost nobody can use. 1013 hPa means
// nothing to a person standing in a room; "falling fast" means a front is
// coming and the washing should come in. Weather stations have shown this as an
// arrow for two centuries and the convention is worth keeping, so the angle is
// the rate: level for steady, and steeper as the three-hour change grows,
// against the Met Office tendency bands (±0.1, ±1.5, ±3.5, ±6.0 hPa/3h).
//
// Drawn as a supersampled shape rather than a rotated bitmap. A bitmap rotated
// to 37 degrees on a 320-pixel panel is a staircase, and this is the element
// the owner asked to be worth looking at.
inline void drawTendencyArrow(FramebufferView framebuffer, int centreX,
                              int centreY, int reach, double angleRadians,
                              int thickness, std::uint16_t colour,
                              std::uint16_t shadow) {
  const double sine = std::sin(angleRadians);
  const double cosine = std::cos(angleRadians);
  const double headLength = reach * 0.9;
  const double headHalf = thickness * 1.9;
  const double shaftHalf = thickness / 2.0;
  // Inside-ness of one sample point, in the arrow's own coordinates: u along the
  // shaft from the tail, v across it.
  const auto inside = [&](double x, double y) {
    const double u = x * cosine + y * sine;
    const double v = -x * sine + y * cosine;
    const double absV = v < 0 ? -v : v;
    if (u < -reach || u > reach) return false;
    if (u <= reach - headLength) return absV <= shaftHalf;
    return absV <= headHalf * (reach - u) / headLength;
  };
  const int span = reach + static_cast<int>(headHalf) + 3;
  // Shadow first, one pixel down and right: the light on every other raised
  // surface on these screens comes from above-left, and an arrow that ignored it
  // would be the only object on the panel lit from somewhere else.
  for (int pass = 0; pass < 2; ++pass) {
    const int offsetX = pass == 0 ? 1 : 0;
    const int offsetY = pass == 0 ? 2 : 0;
    const std::uint16_t ink = pass == 0 ? shadow : colour;
    for (int y = -span; y <= span; ++y) {
      for (int x = -span; x <= span; ++x) {
        int hits = 0;
        for (int subY = 0; subY < 4; ++subY) {
          for (int subX = 0; subX < 4; ++subX) {
            if (inside(x + (subX + 0.5) / 4.0 - 0.5, y + (subY + 0.5) / 4.0 - 0.5)) {
              ++hits;
            }
          }
        }
        if (hits == 0) continue;
        putPixelAlpha(framebuffer, centreX + x + offsetX, centreY + y + offsetY,
                      ink, static_cast<std::uint8_t>(hits > 15 ? 15 : hits));
      }
    }
  }
}

// A single seven-segment digit, drawn from geometry rather than from a font.
//
// The point is legibility at distance: segments are thick bars with rounded
// ends and generous gaps, so the shape survives when the pixels stop resolving
// — which on this panel is at about a metre and a half for body text and three
// metres for these (docs/129 §1). It also scales, which a bitmap face at one
// size does not: the same routine draws the figure at 42 pixels and at 30.
inline void drawSegmentDigit(FramebufferView framebuffer, int left, int top,
                             int width, int height, int stroke, int digit,
                             std::uint16_t colour) {
  if (digit < 0 || digit > 9) return;
  //        A
  //      F   B
  //        G
  //      E   C
  //        D
  static constexpr std::uint8_t kSegments[10] = {
      0b0111111, 0b0000110, 0b1011011, 0b1001111, 0b1100110,
      0b1101101, 0b1111101, 0b0000111, 0b1111111, 0b1101111};
  const std::uint8_t on = kSegments[digit];
  const int half = height / 2;
  const int radius = stroke / 2;
  const auto bit = [&](int index) { return (on & (1U << index)) != 0U; };
  if (bit(0)) fillRoundRect(framebuffer, {left + stroke, top, width - 2 * stroke, stroke}, radius, colour);
  if (bit(6)) fillRoundRect(framebuffer, {left + stroke, top + half - stroke / 2, width - 2 * stroke, stroke}, radius, colour);
  if (bit(3)) fillRoundRect(framebuffer, {left + stroke, top + height - stroke, width - 2 * stroke, stroke}, radius, colour);
  if (bit(5)) fillRoundRect(framebuffer, {left, top + stroke, stroke, half - stroke}, radius, colour);
  if (bit(1)) fillRoundRect(framebuffer, {left + width - stroke, top + stroke, stroke, half - stroke}, radius, colour);
  if (bit(4)) fillRoundRect(framebuffer, {left, top + half, stroke, half - stroke}, radius, colour);
  if (bit(2)) fillRoundRect(framebuffer, {left + width - stroke, top + half, stroke, half - stroke}, radius, colour);
}

// A whole number in those digits, right-aligned to `rightEdge`. Digits falling
// off the left of the panel are simply not drawn: running past the edge is the
// intent, not an accident, and the clipping is what makes a number feel larger
// than the thing containing it.
inline void drawSegmentNumber(FramebufferView framebuffer, int rightEdge, int top,
                              int digitWidth, int height, int stroke, int gap,
                              int value, std::uint16_t colour) {
  int remaining = value < 0 ? 0 : value;
  int cursor = rightEdge - digitWidth;
  do {
    drawSegmentDigit(framebuffer, cursor, top, digitWidth, height, stroke,
                     remaining % 10, colour);
    remaining /= 10;
    cursor -= digitWidth + gap;
  } while (remaining > 0 && cursor > -digitWidth);
}

// The same digits with one decimal place, for a temperature that has to match
// the CO2 reading beside it. A value set in type next to a value set in
// segments reads as two different kinds of fact; this makes them one.
inline void drawSegmentDecimal(FramebufferView framebuffer, int rightEdge, int top,
                               int digitWidth, int height, int stroke, int gap,
                               int centi, std::uint16_t colour) {
  const int value = centi < 0 ? -centi : centi;
  const int whole = value / 100;
  const int tenth = (value / 10) % 10;
  int cursor = rightEdge - digitWidth;
  drawSegmentDigit(framebuffer, cursor, top, digitWidth, height, stroke, tenth, colour);
  // The point sits on the baseline, the size of a stroke, which is what a
  // seven-segment display does and what keeps it from looking like a comma.
  cursor -= gap + stroke;
  fillRoundRect(framebuffer, {cursor, top + height - stroke, stroke, stroke},
                stroke / 2, colour);
  cursor -= gap;
  int remaining = whole;
  do {
    cursor -= digitWidth;
    drawSegmentDigit(framebuffer, cursor, top, digitWidth, height, stroke,
                     remaining % 10, colour);
    remaining /= 10;
    cursor -= gap;
  } while (remaining > 0 && cursor > -digitWidth);
}

// The project's own mark, from brand/logo-a-signal-cube-micro.svg, drawn from
// its geometry at whatever size is asked for.
//
// Generated rather than stored because the alternative was what the About
// screen carried until now: a blue square with the letters "A2" in it, which
// means nothing to anybody and is exactly the placeholder-that-shipped the
// owner calls slop. The mark is a rounded body with two holes and an arc cut
// out of it, and two amber points sitting in the holes — a signal leaving a
// cube, which is what the thing does.
inline void drawSignalCube(FramebufferView framebuffer, int left, int top, int size,
                           std::uint16_t body, std::uint16_t hole,
                           std::uint16_t point) {
  const double scale = size / 32.0;
  const auto px = [&](double value) { return left + static_cast<int>(value * scale + 0.5); };
  const auto py = [&](double value) { return top + static_cast<int>(value * scale + 0.5); };
  const auto len = [&](double value) { return static_cast<int>(value * scale + 0.5); };

  fillRoundRect(framebuffer, {px(2), py(2), len(28), len(28)}, len(8), body);
  // The cut-outs, painted in the colour behind the mark.
  fillCircle(framebuffer, px(9), py(16), len(3), hole);
  fillRoundRect(framebuffer, {px(25), py(13), len(7), len(6)}, len(3), hole);
  // The arc, sampled along the quadratic the SVG describes.
  for (int step = 0; step <= 48; ++step) {
    const double travel = step / 48.0;
    const double rest = 1.0 - travel;
    const double curveX = rest * rest * 15.0 + 2.0 * rest * travel * 23.0 + travel * travel * 15.0;
    const double curveY = rest * rest * 10.0 + 2.0 * rest * travel * 16.0 + travel * travel * 22.0;
    fillCircle(framebuffer, px(curveX), py(curveY), len(1.5), hole);
  }
  fillCircle(framebuffer, px(9), py(16), len(1.75), point);
  fillCircle(framebuffer, px(27), py(16), len(1.25), point);
}


const char* translated(const CompactSnapshot& snapshot, const char* polish,
                       const char* english) {
  return snapshot.locale == LocaleMode::en ? english : polish;
}

const char* metadataTitle(const CompactSnapshot& snapshot) {
  if (snapshot.metadata_available && snapshot.now_playing_title != nullptr &&
      snapshot.now_playing_title[0] != '\0') {
    return snapshot.now_playing_title;
  }
  return translated(snapshot, "Tytuł niedostępny", "Title unavailable");
}

struct Painter {
  FramebufferView framebuffer;
  std::uint16_t truncated = 0;

  TextRenderResult text(int x, int y, const char* value, FontSize size,
                        std::uint16_t color, int maximum_width,
                        bool ellipsize = true) {
    const auto result = drawText(framebuffer, x, y, value, size, color,
                                 maximum_width, ellipsize);
    if (result.truncated) ++truncated;
    return result;
  }

  TextRenderResult centered(int center_x, int y, const char* value,
                            FontSize size, std::uint16_t color,
                            int maximum_width) {
    const int width = std::min(measureText(value, size), maximum_width);
    return text(center_x - width / 2, y, value, size, color, maximum_width);
  }
};

void drawWrappedText(Painter& painter, int x, int y, const char* value,
                     FontSize size, std::uint16_t color, int maximum_width,
                     int maximum_lines) {
  if (value == nullptr || maximum_lines <= 0) return;
  std::array<char, 193> buffer{};
  std::strncpy(buffer.data(), value, buffer.size() - 1);
  char* line = buffer.data();
  for (int line_index = 0; line_index < maximum_lines && *line != '\0';
       ++line_index) {
    auto* cursor = reinterpret_cast<const unsigned char*>(line);
    char* last_space = nullptr;
    char* fit_end = line;
    int width = 0;
    while (*cursor != 0U) {
      const auto* start = cursor;
      const auto codepoint = decodeUtf8(cursor);
      const auto& glyph = glyphFor(generated::interFace(size), codepoint);
      if (width + glyph.advance > maximum_width) break;
      width += glyph.advance;
      fit_end = reinterpret_cast<char*>(const_cast<unsigned char*>(cursor));
      if (codepoint == ' ') last_space = reinterpret_cast<char*>(const_cast<unsigned char*>(start));
    }
    if (*cursor == 0U) {
      painter.text(x, y + line_index * lineHeight(size), line, size, color,
                   maximum_width, false);
      break;
    }
    if (line_index == maximum_lines - 1) {
      painter.text(x, y + line_index * lineHeight(size), line, size, color,
                   maximum_width, true);
      break;
    }
    char* split = last_space != nullptr && last_space > line ? last_space : fit_end;
    if (split <= line) {
      painter.text(x, y + line_index * lineHeight(size), line, size, color,
                   maximum_width, true);
      break;
    }
    const char saved = *split;
    *split = '\0';
    painter.text(x, y + line_index * lineHeight(size), line, size, color,
                 maximum_width, false);
    *split = saved;
    line = split;
    while (*line == ' ') ++line;
  }
}

void drawUtilityHeader(Painter& painter, const ThemeTokens& theme,
                       const char* title, const char* subtitle = nullptr) {
  // On the grid, like everything else. This header is shared by nineteen
  // screens and sat at x=12 with its subtitle at x=14, so nineteen screens
  // began two to five pixels inside the margin their own content used — which
  // the geometry audit reported as nineteen separate findings from one cause.
  painter.text(kHomeMargin, 8, title, FontSize::title, theme.text,
               kScreenWidth - kHomeMargin - 56);
  if (subtitle != nullptr) {
    painter.text(kHomeMargin, 31, subtitle, FontSize::caption, theme.muted,
                 kScreenWidth - kHomeMargin - 80);
  }
  // Drawn to MATCH its hit box, which is x >= 268 and y < 48 — 52 by 48,
  // reaching two edges of the glass. It used to be painted as 44 by 44 inset at
  // y=2, so the visible centre and the real centre were different points and
  // aiming at what you can see meant aiming slightly off what responds.
  fillRoundRect(painter.framebuffer, {268, 0, 52, 48}, 14, theme.raised);
  drawCross(painter.framebuffer, 292, 24, 9, 3, theme.text);
}

void drawNoiseGlyph(FramebufferView framebuffer, int center_x, int center_y,
                    std::uint16_t foreground, std::uint16_t background) {
  fillCircle(framebuffer, center_x, center_y, 10, foreground);
  fillCircle(framebuffer, center_x, center_y, 7, background);
  fillCircle(framebuffer, center_x, center_y, 5, foreground);
  fillCircle(framebuffer, center_x, center_y, 2, background);
}

// Copies the first UTF-8 code point of text into out (used for the station
// monogram / logo-slot placeholder initial). out must hold at least 5 bytes.
void copyFirstGlyph(const char* text, char* out, std::size_t capacity) {
  if (out == nullptr || capacity == 0) return;
  out[0] = '\0';
  if (text == nullptr || text[0] == '\0') return;
  const auto* bytes = reinterpret_cast<const unsigned char*>(text);
  std::size_t length = 1;
  if ((bytes[0] & 0xe0U) == 0xc0U) length = 2;
  else if ((bytes[0] & 0xf0U) == 0xe0U) length = 3;
  else if ((bytes[0] & 0xf8U) == 0xf0U) length = 4;
  std::size_t i = 0;
  for (; i < length && i + 1 < capacity && bytes[i] != 0U; ++i) {
    out[i] = static_cast<char>(bytes[i]);
  }
  out[i] = '\0';
}

namespace {
const RuntimeStationLogo* runtime_logos = nullptr;
std::size_t runtime_logo_count = 0;
}  // namespace

namespace {

// Blits a runtime logo into rect: cover-fit, centred, nearest-neighbour,
// clipped to the same rounded outline the slot beneath it was filled with.
// Nearest is deliberate — the source is at most 96x96 and the smallest slot
// is 30 px, and a deterministic integer resampler keeps this path trivially
// auditable against the golden-hash gate (which never exercises it).
bool drawRuntimeStationLogo(Painter& painter, Rect rect, int radius,
                            std::size_t index) {
  if (runtime_logos == nullptr || index >= runtime_logo_count) return false;
  const auto& logo = runtime_logos[index];
  if (logo.pixels == nullptr || logo.width == 0 || logo.height == 0) return false;
  radius = std::min(radius, std::min(rect.width, rect.height) / 2);
  const int radius_squared = radius * radius;
  // Cover, by owner decision: the mark fills the whole block. Every target
  // pixel samples the source scaled by the LARGER axis ratio, centred, so the
  // overflow crops instead of leaving bars. Sources are square cover-crops
  // already, which makes this exact for square slots and a mild centre-crop
  // for the rectangular hero.
  for (int y = 0; y < rect.height; ++y) {
    for (int x = 0; x < rect.width; ++x) {
      if (radius > 0) {
        // Same corner-arc test as fillRoundRect, so the logo edge lands on
        // exactly the pixels the slot fill painted and the frame around it
        // stays visible.
        const int corner_x = x < radius
                                 ? radius - 1 - x
                                 : x >= rect.width - radius
                                       ? x - (rect.width - radius)
                                       : 0;
        const int corner_y = y < radius
                                 ? radius - 1 - y
                                 : y >= rect.height - radius
                                       ? y - (rect.height - radius)
                                       : 0;
        if (corner_x != 0 && corner_y != 0 &&
            corner_x * corner_x + corner_y * corner_y >= radius_squared) {
          continue;
        }
      }
      int source_x;
      int source_y;
      if (rect.width * logo.height >= rect.height * logo.width) {
        source_x = (x * logo.width) / rect.width;
        const int scaled_h = (rect.width * logo.height) / logo.width;
        source_y = ((y + (scaled_h - rect.height) / 2) * logo.width) / rect.width;
      } else {
        source_y = (y * logo.height) / rect.height;
        const int scaled_w = (rect.height * logo.width) / logo.height;
        source_x = ((x + (scaled_w - rect.width) / 2) * logo.height) / rect.height;
      }
      if (source_x < 0) source_x = 0;
      if (source_y < 0) source_y = 0;
      if (source_x >= logo.width) source_x = logo.width - 1;
      if (source_y >= logo.height) source_y = logo.height - 1;
      putPixel(painter.framebuffer, rect.x + x, rect.y + y,
               logo.pixels[source_y * logo.width + source_x]);
    }
  }
  return true;
}

}  // namespace

#if defined(OPEN_RADIO_HAS_LOGO_PACK)
// Blits the baked station logo centred in rect, choosing the mip whose size is
// nearest the rect (no runtime scaling). Returns false when no logo is baked
// for this station so the caller can fall back to the placeholder.
bool drawStationLogo(Painter& painter, Rect rect, std::size_t index) {
  if (index >= generated::kStationLogos.size()) return false;
  const auto& logo = generated::kStationLogos[index];
  if (!logo.present) return false;
  const int target = std::min(rect.width, rect.height);
  std::size_t best = 0;
  for (std::size_t level = 1; level < logo.mips.size(); ++level) {
    const int here = generated::kStationLogoSizes[level] - target;
    const int keep = generated::kStationLogoSizes[best] - target;
    if ((here < 0 ? -here : here) < (keep < 0 ? -keep : keep)) best = level;
  }
  const auto& mip = logo.mips[best];
  const int origin_x = rect.x + (rect.width - mip.width) / 2;
  const int origin_y = rect.y + (rect.height - mip.height) / 2;
  for (std::uint16_t run_index = 0; run_index < mip.run_count; ++run_index) {
    const auto& run = mip.runs[run_index];
    fillRect(painter.framebuffer,
             {origin_x + run.x, origin_y + run.y, run.length, 1},
             mip.palette[run.color]);
  }
  return true;
}
#endif

void drawStationBadge(Painter& painter, const ThemeTokens& theme,
                      const StationTheme& station, Rect bounds,
                      [[maybe_unused]] std::size_t index) {
  fillRoundRect(painter.framebuffer, bounds, 24, theme.brand);
  fillRoundRect(painter.framebuffer,
                {bounds.x + 4, bounds.y + 4, bounds.width - 8,
                 bounds.height - 8},
                20, theme.surface);
  // The logo fills the badge INTERIOR — the same rect and rounding as the
  // surface fill above — so the accent ring stays a visible frame around it.
  if (drawRuntimeStationLogo(painter,
                             {bounds.x + 4, bounds.y + 4, bounds.width - 8,
                              bounds.height - 8},
                             20, index)) {
    return;
  }
#if defined(OPEN_RADIO_HAS_LOGO_PACK)
  if (drawStationLogo(painter, bounds, index)) return;
#endif
  fillCircle(painter.framebuffer, bounds.x + bounds.width / 2,
             bounds.y + bounds.height / 2, bounds.width / 3, theme.raised);
  const int center_x = bounds.x + bounds.width / 2;
  const int center_y = bounds.y + bounds.height / 2;
  const int inner_width = bounds.width - 16;
  // Scale the placeholder mark to the badge: pick the heaviest weight that fits
  // the short name (a hero badge earns the display weight), dropping to a
  // single-letter monogram only when even body overflows.
  const FontSize ceiling =
      bounds.width >= 100 ? FontSize::display : FontSize::title;
  std::array<char, 12> label{};
  FontSize size = ceiling;
  bool fits = false;
  for (int level = static_cast<int>(ceiling);
       level >= static_cast<int>(FontSize::body); --level) {
    const auto candidate = static_cast<FontSize>(level);
    if (measureText(station.short_name, candidate) <= inner_width) {
      size = candidate;
      std::snprintf(label.data(), label.size(), "%s", station.short_name);
      fits = true;
      break;
    }
  }
  if (!fits) copyFirstGlyph(station.short_name, label.data(), label.size());
  painter.centered(center_x, center_y - generated::interFace(size).ascent / 2,
                   label.data(), size, theme.text, inner_width);
}

// The blue pill was the loudest survivor of the pre-brand chrome. The owner's
// ruling (2026-07-30, "B, extended"): the brand reaches whatever 0.4 can
// carry, so the pill becomes a ghost — an accent stroke around raised night,
// the same construction as the station tile's frame two bands up. The label
// keeps cream; on_brand had nothing left to sit on.
void drawTransport(Painter& painter, const CompactSnapshot& snapshot,
                   const ThemeTokens& theme) {
  fillRect(painter.framebuffer, {0, 184, 320, 56}, theme.surface);
  fillRoundRect(painter.framebuffer, {8, 190, 52, 44}, 14, theme.raised);
  fillRoundRect(painter.framebuffer, {68, 190, 184, 44}, 14, theme.brand);
  fillRoundRect(painter.framebuffer, {70, 192, 180, 40}, 13, theme.raised);
  fillRoundRect(painter.framebuffer, {260, 190, 52, 44}, 14, theme.raised);
  drawIconMask24(painter.framebuffer, 22, 200, generated::kIconChevronLeft,
                 theme.text);
  drawIconMask24(painter.framebuffer, 274, 200, generated::kIconChevronRight,
                 theme.text);
  painter.centered(160, 201, translated(snapshot, "Stacje", "Stations"),
                   FontSize::body, theme.text, 140);
}

void formatClock(const CompactSnapshot& snapshot, std::array<char, 8>& time,
                 std::array<char, 20>& date) {
  if (!snapshot.clock_valid) {
    std::snprintf(time.data(), time.size(), "--:--");
    std::snprintf(date.data(), date.size(), "%s",
                  snapshot.locale == LocaleMode::pl ? "Czas nieustawiony"
                                                    : "Time not set");
    return;
  }
  std::snprintf(time.data(), time.size(), "%02u:%02u", snapshot.clock_hour,
                snapshot.clock_minute);
  std::snprintf(date.data(), date.size(), "%02u.%02u.%04u",
                snapshot.clock_day, snapshot.clock_month, snapshot.clock_year);
}

// The gas calibration, said in the advice band — the owner's wording with the
// live figures: what is calibrating, what it reads now, how long it has been
// at it. The end is a criterion (ten quiet minutes), not a promised clock, so
// no finish time is printed. Yields to a real alert without a fight: the band
// carries one sentence.
void drawGasCalibrationNotice(Painter& painter, const CompactSnapshot& snapshot,
                              const ThemeTokens& theme) {
  if (snapshot.air_alert != nullptr && snapshot.air_alert_level >= 1) return;
  if (snapshot.sensor_gas_resistance_ohm == kSensorAbsent) return;
  if (snapshot.sensor_gas_departure_percent != kSensorAbsent) return;
  std::array<char, 56> line{};
  if (snapshot.sensor_gas_warmup_minutes != kSensorAbsent) {
    std::snprintf(line.data(), line.size(), "%s %ld kOhm \u00b7 %ld min",
                  translated(snapshot, "Kalibracja gazów:", "Gas calibrating:"),
                  static_cast<long>(snapshot.sensor_gas_resistance_ohm / 1000),
                  static_cast<long>(snapshot.sensor_gas_warmup_minutes));
  } else {
    std::snprintf(line.data(), line.size(), "%s",
                  translated(snapshot, "Kalibracja czujnika gazów",
                             "Gas sensor calibrating"));
  }
  painter.text(kHomeMargin,
               centredIn(kBandAdviceTop, kBandAdviceHeight, kCapBody) - kInkTopBody,
               line.data(), FontSize::body, theme.muted, kHomeContent);
}

// The single alert the device has decided to say, in the advice band.
//
// Colour by LEVEL rather than by which reading produced it: a person learns one
// mapping — amber means look at this when convenient, red means look now — and
// learns it once, whether the sentence is about CO2, damp or the gas element.
// The levels are open_radio::AlertLevel; Ambient never reaches this band,
// because "recolour something already on screen" is the whole of its permission.
void drawAirAlert(Painter& painter, const CompactSnapshot& snapshot,
                  const ThemeTokens& theme) {
  if (snapshot.air_alert == nullptr) return;
  if (snapshot.air_alert_level < 1) return;
  const std::uint16_t colour =
      snapshot.air_alert_level >= 3 ? theme.danger
      : snapshot.air_alert_level == 2 ? theme.warning
                                      : theme.text;
  painter.text(kHomeMargin,
               centredIn(kBandAdviceTop, kBandAdviceHeight, kCapBody) - kInkTopBody,
               // The whole content width: this band holds one sentence and
               // nothing else, and a sentence clipped to "Powietrze krąży tu…"
               // has had its verb removed.
               snapshot.air_alert, FontSize::body, colour, kHomeContent);
}

// Which way the weather is going, in the space the volume ring used to hold.
//
// The tendency BANDS are the Met Office's, which is also what every shipping
// forecast has meant by "falling slowly" since long before any of this: steady
// under 0.1 hPa in three hours, then slowly, then steadily, then quickly, then
// very rapidly at 6.0 and above. The arrow's angle is that band, so the shape
// says the thing before the number is read — which is the entire reason a
// barometer has a needle and not a display.
//
// Three states, and the third is the one that matters: pressure and a tendency
// draws the arrow; pressure without three hours of history draws the figure
// alone; no module draws nothing at all. An arrow lying flat because the device
// has only been on for ten minutes would be a false statement, and a confident
// one.
void drawPressureTendency(Painter& painter, const CompactSnapshot& snapshot,
                          const ThemeTokens& theme, int centreX, int bandTop) {
  if (snapshot.sensor_pressure_centi_hectopascal == kSensorAbsent) return;
  std::array<char, 16> value{};
  std::snprintf(value.data(), value.size(), "%ld hPa",
                static_cast<long>(snapshot.sensor_pressure_centi_hectopascal / 100));

  const std::int32_t change = snapshot.sensor_pressure_change_centi_hpa_3h;
  if (change == kSensorAbsent) {
    painter.centered(centreX, centredIn(bandTop, kBandHeaderHeight, kAscentCaption),
                     value.data(), FontSize::caption, theme.muted, 86);
    return;
  }

  const std::int32_t magnitude = change < 0 ? -change : change;
  const bool rising = change > 0;
  // Degrees off the horizontal, one per tendency band.
  const double degrees = magnitude < 10    ? 0.0
                         : magnitude < 150 ? 20.0
                         : magnitude < 350 ? 38.0
                         : magnitude < 600 ? 55.0
                                           : 70.0;
  // Screen y grows downwards, so rising pressure is a negative angle.
  const double radians = (rising ? -degrees : degrees) * 3.14159265358979 / 180.0;
  // Falling is the direction worth colouring. A barometer dropping 6 hPa in
  // three hours is the classic front warning, and it is the one case where the
  // arrow should catch an eye that was not looking for it.
  const std::uint16_t colour = magnitude < 150 ? theme.text
                               : !rising && magnitude >= 600 ? theme.danger
                               : !rising && magnitude >= 350 ? theme.warning
                                                             : theme.success;
  drawTendencyArrow(painter.framebuffer, centreX, bandTop + 18, 15, radians, 5,
                    colour, theme.surface);
  painter.centered(centreX, bandTop + 40, value.data(), FontSize::caption,
                   theme.muted, 86);
}

// How many attached modules are publishing. Two screens and one settings tile
// ask the same question and each counted it themselves.
int modulesOk(const CompactSnapshot& snapshot) {
  int count = 0;
  for (const auto status : snapshot.sensor_status) {
    if (status == 2) ++count;
  }
  return count;
}

// The CO2 scale, and the gas hairline under it. Shared by the minimal and air
// homes so the one element that reads across a room cannot come out different
// on two screens that both claim to show it.
void drawAirScale(Painter& painter, const CompactSnapshot& snapshot,
                  const ThemeTokens& theme) {
  if (snapshot.sensor_co2_ppm == kSensorAbsent) return;
    // BAND 3 — the bar, which is the only part that reads across a room.
    const std::int32_t ppm = snapshot.sensor_co2_ppm;
    const std::int32_t clamped = ppm < 400 ? 400 : ppm > 2000 ? 2000 : ppm;
    const int width = static_cast<int>((clamped - 400) * kHomeContent / 1600);
    // The band holds one scale when there is one module and two when there are
    // two: CO2 keeps the full 18 pixels alone, and gives up 6 of them to a
    // strip underneath when a gas MODULE is present. Keyed on the module, not
    // on the departure: the departure is absent through the whole burn-in, and
    // a second storey that only appeared half an hour after the hub went in
    // read as "the second bar is missing" (owner, 2026-07-30). The BAND does
    // not move either way, so nothing below it shifts when the hub is plugged
    // in — and only once, when it is.
    const bool hasGasModule = snapshot.sensor_gas_resistance_ohm != kSensorAbsent;
    const int co2BarHeight = hasGasModule ? 12 : kBandBarHeight;
    const SpectrumInk ink = spectrumInk(snapshot, theme);
    drawSpectrumBar(painter.framebuffer,
                    {kHomeMargin, kBandBarTop, kHomeContent, co2BarHeight},
                    co2BarHeight / 2, width, ink.fresh, ink.warn,
                    ink.danger, theme.surface);
    for (const std::int32_t threshold : {1000, 1400}) {
      const int x = kHomeMargin +
                    static_cast<int>((threshold - 400) * kHomeContent / 1600);
      fillRect(painter.framebuffer, {x, kBandBarTop, 2, co2BarHeight / 3},
               threshold <= clamped ? theme.canvas : theme.muted);
    }

    // The gas element's DEPARTURE from its own running median, as the second
    // storey of the band. Never its absolute resistance: a metal-oxide element
    // drifts for months after it is powered on, so the raw number means
    // nothing and only the change from where it has been sitting does.
    //
    // The TRACK is always there — the storey exists the moment the module
    // does, which is what says "two sensors" at a glance. The FILL follows the
    // full-size gas band exactly: pollution only (resistance below the room's
    // own median), the same spectrum, the same 30/50 calibration ticks. During
    // burn-in the departure is absent and the track simply stays empty, which
    // is the truthful face of "still learning".
    if (hasGasModule) {
      constexpr int kGasTop = kBandBarTop + 14;
      constexpr int kGasHeight = 4;
      const std::int32_t departure = snapshot.sensor_gas_departure_percent;
      const std::int32_t pollution =
          departure == kSensorAbsent ? 0 : departure < 0 ? -departure : 0;
      const int span = pollution > 100 ? 100 : static_cast<int>(pollution);
      drawSpectrumBar(painter.framebuffer,
                      {kHomeMargin, kGasTop, kHomeContent, kGasHeight},
                      kGasHeight / 2, kHomeContent * span / 100, ink.fresh,
                      ink.warn, ink.danger, theme.surface, 300, 500);
      for (const int tick : {30, 50}) {
        fillRect(painter.framebuffer,
                 {kHomeMargin + kHomeContent * tick / 100, kGasTop, 2, kGasHeight},
                 tick <= span ? theme.canvas : theme.muted);
      }
    }

}

// The two large figures on one baseline at one size.
void drawAirFigures(Painter& painter, const CompactSnapshot& snapshot,
                    const ThemeTokens& theme) {
  if (snapshot.sensor_co2_ppm == kSensorAbsent) return;
  const std::uint16_t bandColour =
      snapshot.sensor_air_band >= 4   ? theme.danger
      : snapshot.sensor_air_band == 3 ? theme.warning
      : snapshot.sensor_air_band == 0 ? theme.muted
                                      : theme.success;
  const std::int32_t ppm = snapshot.sensor_co2_ppm;
    // BAND 5 — the two large figures, ONE top and ONE size, which is the
    // whole of the owner's point. They were 22 pixels apart vertically and it
    // read as two screens overlapping; equal readings get equal treatment or
    // the layout is making a claim about them that nobody meant.
    const bool atCeilingMinimal =
        snapshot.sensor_co2_ceiling_ppm != kSensorAbsent &&
        ppm >= snapshot.sensor_co2_ceiling_ppm;
    // Both figures sit ON the grid rather than near it: the temperature block
    // opens at the left margin and the CO2 block closes at the right one, so
    // the row is bounded by the same two edges as the bar above it. Their
    // units share those edges instead of being centred under them — a label
    // that lines up with its figure's own edge is a stronger alignment than
    // one floating in the middle, and it is the alignment the rest of the
    // screen already uses.
    constexpr int kDigitWidth = 22;
    constexpr int kDigitGap = 4;
    // The point and its two gaps, measured from the rendered block rather than
    // derived: drawSegmentDecimal spaces it by eye and 84 leaves the leading
    // digit one pixel inside the margin.
    constexpr int kDecimalPoint = 11;
    constexpr int kCo2Right = kHomeMargin + kHomeContent - 1;
    constexpr int kTemperatureRight =
        kHomeMargin + (3 * kDigitWidth + 2 * kDigitGap + kDecimalPoint) - 1;

    drawSegmentNumber(painter.framebuffer, kCo2Right, kBandDigitsTop, kDigitWidth,
                      kBandDigitsHeight, 6, kDigitGap, static_cast<int>(ppm),
                      bandColour);
    {
      const char* unit = atCeilingMinimal ? "ppm +" : "ppm";
      painter.text(kCo2Right - measureText(unit, FontSize::caption) + 1,
                   kBandUnitsTop, unit, FontSize::caption, theme.muted, 60);
    }

    if (snapshot.sensor_temperature_centi_celsius != kSensorAbsent) {
      drawSegmentDecimal(painter.framebuffer, kTemperatureRight, kBandDigitsTop,
                         kDigitWidth, kBandDigitsHeight, 6, kDigitGap,
                         static_cast<int>(snapshot.sensor_temperature_centi_celsius),
                         theme.text);
      painter.text(kHomeMargin, kBandUnitsTop, "°C", FontSize::caption,
                   theme.muted, 60);
    }

}

// Humidity as the floor of the panel.
void drawHumidityFloor(Painter& painter, const CompactSnapshot& snapshot,
                       const ThemeTokens& theme) {
    // BAND 6 — humidity as the floor of the screen: edge to edge, no margins,
    // no corner radius, no figure beside it. The owner asked for a strip that
    // fills completely, and a strip that stopped 8 pixels short of the glass
    // at each end was reading as one more floating object on a screen that had
    // too many. It is also the only element allowed to cross the margin, and
    // it crosses it all the way, which is the difference between a bleed and a
    // mistake.
    //
    // The colour is the humidity BAND, not a fixed blue: too dry and too damp
    // are the two states worth noticing, and the strip is the only thing on
    // the screen that can say so without spending a word on it.
    if (snapshot.sensor_humidity_centi_percent != kSensorAbsent) {
      const std::int32_t centiPercent = snapshot.sensor_humidity_centi_percent;
      const int humidity = static_cast<int>(centiPercent / 100);
      const int clampedHumidity = humidity < 0 ? 0 : humidity > 100 ? 100 : humidity;
      const std::uint16_t humidityColour = centiPercent < 3000  ? theme.warning
                                           : centiPercent > 6000 ? theme.danger
                                                                 : theme.brand;
      fillRect(painter.framebuffer,
               {0, kBandHumidityTop, 320, kBandHumidityHeight}, theme.surface);
      const int filled = 320 * clampedHumidity / 100;
      if (filled > 0) {
        fillRect(painter.framebuffer,
                 {0, kBandHumidityTop, filled, kBandHumidityHeight},
                 humidityColour);
      }
    }
}

// The gas element, given the same stage as the CO2 scale — and no more truth
// than it owns.
//
// A metal-oxide element cannot say "the air holds X"; its absolute resistance
// drifts for months and names no substance. What it CAN say, honestly, is how
// far the air stands from ITS OWN recent normal — so this bar's axis is the
// departure from the running median, in the polluting direction, 0..100%. The
// ticks sit at 30 and at 50, where the alert engine's D1 fires (resistance
// halved). Same capsule, same position-owned colour as the ppm scale, because
// the owner asked for the second sensor to be a peer of the first — and it is
// one, on the one axis it can honestly occupy.
//
// Before the median exists the bar SAYS it is learning instead of being
// invisible: an element that only ever appears mid-crisis is an element nobody
// has learned to read.
void drawGasBand(Painter& painter, const CompactSnapshot& snapshot,
                 const ThemeTokens& theme, int top) {
  if (snapshot.sensor_gas_resistance_ohm == kSensorAbsent) return;
  if (snapshot.sensor_gas_departure_percent == kSensorAbsent) {
    fillRoundRect(painter.framebuffer, {kHomeMargin, top, kHomeContent, kBandBarHeight},
                  kBandBarHeight / 2, theme.surface);
    // The words moved OUT of the bar: caption-on-track was invisible on the
    // light theme at arm's length (owner, 2026-07-30). The bar stays a clean
    // empty track while learning; the sentence lives in the advice band via
    // drawGasCalibrationNotice, at body contrast.
    return;
  }
  const std::int32_t departure = snapshot.sensor_gas_departure_percent;
  // Only the polluting direction fills: resistance FALLING below the median is
  // reducing gas rising. Air cleaner than its own normal reads as an empty
  // scale, not as negative pollution.
  const std::int32_t pollution = departure < 0 ? -departure : 0;
  const int span = pollution > 100 ? 100 : static_cast<int>(pollution);
  const SpectrumInk ink = spectrumInk(snapshot, theme);
  drawSpectrumBar(painter.framebuffer, {kHomeMargin, top, kHomeContent, kBandBarHeight},
                  kBandBarHeight / 2, kHomeContent * span / 100, ink.fresh,
                  ink.warn, ink.danger, theme.surface, 300, 500);
  for (const int tick : {30, 50}) {
    fillRect(painter.framebuffer, {kHomeMargin + kHomeContent * tick / 100, top, 2,
                                   kBandBarHeight / 3},
             tick <= span ? theme.canvas : theme.muted);
  }
  // No figure on the bar. A length IS the value — the same rule the humidity
  // strip follows, and the same one that removed the ppm headline two screens
  // ago. The ticks at 30 and 50 give the scale its calibration.
}

// The header shared by every home screen: which station, what time, which way
// the weather is going, and the way out.
//
// Extracted on 2026-07-30 because the air screen had its own — a 36-pixel badge
// at y=4, a title-size clock at 54, signal bars at 158, a volume ring at 226 and
// a menu button at y=0 — and the owner's word for it was correct. Two screens
// that mean the same row cannot each own a copy of it; the copies drift, and
// drift at this size reads as slop no amount of nudging one element fixes.
void drawHomeHeader(Painter& painter, const CompactSnapshot& snapshot,
                    const ThemeTokens& theme, const StationTheme& station,
                    const std::array<char, 8>& time,
                    const std::array<char, 20>& date) {
  drawStationBadge(painter, theme, station,
                   {kHomeMargin, kBandHeaderTop, kBandHeaderHeight, kBandHeaderHeight},
                   snapshot.station_index);
  fillRoundRect(painter.framebuffer, {268, kBandHeaderTop, 44, 44}, kHomeRadius,
                theme.raised);
  drawIconLit(painter.framebuffer, 282, kBandHeaderTop + 14, generated::kIconMenu2,
              theme.brand, theme.canvas);
  constexpr int kClockStack = kAscentDisplay + 4 + kAscentCaption;
  constexpr int kClockTop = centredIn(kBandHeaderTop, kBandHeaderHeight, kClockStack);
  painter.text(72, kClockTop, time.data(), FontSize::display, theme.text, 110);
  painter.text(72, kClockTop + kAscentDisplay + 4, date.data(), FontSize::caption,
               theme.muted, 110);
  drawPressureTendency(painter, snapshot, theme, 222, kBandHeaderTop);
}

void drawNowPlaying(Painter& painter, const CompactSnapshot& snapshot,
                    const ThemeTokens& theme, const StationTheme& station) {
  fill(painter.framebuffer, theme.canvas);
  if (snapshot.variant == HomeVariant::glance) {
    // V2, to the owner's brief of 2026-07-29. Every change here is a REMOVAL,
    // and the space each removal frees goes to a reading.
    //
    //   - The station's text name is gone. The logo IS the name, and whoever
    //     chose that tile knows it.
    //   - "Playing via Bluetooth" is gone. The route is a state, not news; it
    //     had a whole line of a two-inch screen, every second, for ever.
    //   - The output/volume/Wi-Fi card is gone. Volume becomes a bar and the
    //     network a pip: they answer "on, and roughly where", which is all
    //     either was ever read for.
    //   - The clock takes the space the name had, at display size, because a
    //     cube on a shelf is a clock most of the time it is looked at.
    //
    // What that buys is the middle of the screen, and the middle goes to the
    // air — but only when something is measuring it. With no module the
    // metadata card simply moves up and nothing apologises for hardware the
    // owner did not buy.
    std::array<char, 8> time{};
    std::array<char, 20> date{};
    formatClock(snapshot, time, date);

    // BAND 1 — the shared header. The volume ring and the Wi-Fi bars that stood
    // between the clock and the menu were removed on 2026-07-30 by owner
    // decision, and the decision is right for a reason worth writing down: both
    // were the state of a CONTROL, and the home screen of an appliance belongs
    // to what it is doing — the programme and the air — not to how its knobs
    // are set. Volume is one tap away in the menu and shows itself the moment it
    // changes; the signal moved into that same menu, beside the noise button.
    //
    // What the removal buys is 86 pixels between the clock and the menu, and
    // that is where the second module goes. So the answer to "will the ENV Pro
    // fit on minimal" is yes, and this is the room it fits in.
    drawHomeHeader(painter, snapshot, theme, station, time, date);

    // BAND 2 — what is playing. This was the volume bar until the owner pointed
    // out that a radio's home screen was giving its largest band to a control
    // and none at all to the programme.
    fillRoundRect(painter.framebuffer,
                  {kHomeMargin, kBandTitleTop, kHomeContent, kBandTitleHeight},
                  kHomeRadius, theme.surface);
    // Centred on the CAPITALS, which is what an eye centres on, rather than on
    // the cell the font reserves. The cell carries descender room whether the
    // line has a "j" in it or not; centring it therefore puts a line without one
    // low and a line with one high, and the title changes every three minutes,
    // so the wobble is permanent. Measured: ink begins kInkTopBody into the
    // cell, capitals are kCapBody tall, a wrapped second line kLineAdvanceBody
    // below the first.
    {
      const int lines = measureText(metadataTitle(snapshot), FontSize::body) >
                                kHomeContent - 24
                            ? 2
                            : 1;
      const int capBlock = lines == 2 ? kLineAdvanceBody + kCapBody : kCapBody;
      drawWrappedText(painter, kHomeMargin + 12,
                      centredIn(kBandTitleTop, kBandTitleHeight, capBlock) -
                          kInkTopBody,
                      metadataTitle(snapshot), FontSize::body, theme.text,
                      kHomeContent - 24, 2);
    }

    const int modules = modulesOk(snapshot);
    const bool hasCo2 = modules > 0 && snapshot.sensor_co2_ppm != kSensorAbsent;
    const bool hasTemperature =
        modules > 0 && snapshot.sensor_temperature_centi_celsius != kSensorAbsent;

    if (hasCo2) {
      drawAirScale(painter, snapshot, theme);

      // BAND 4 — the advice, on its own line, which is what bought the space to
      // put the two large figures on one baseline. It used to sit at y=160
      // beside the temperature and the two fought over the same corner.
      //
      // The sentence arrives already chosen and already worded. It used to be
      // decided here from the band, which meant the screen was quietly the
      // sixteenth place in the codebase that knew what 1000 ppm means; the rules
      // and their hysteresis now live in one file (docs/131) and this draws
      // whatever came out of them.
      drawAirAlert(painter, snapshot, theme);
      drawGasCalibrationNotice(painter, snapshot, theme);

      drawAirFigures(painter, snapshot, theme);
      drawHumidityFloor(painter, snapshot, theme);
    } else if (hasTemperature) {
      // No CO2 — an ENV Pro on its own, and the same stage set the CO2 pair
      // gets: two large figures on one baseline at one size, the alert line
      // above them, the humidity floor below. The first version here was four
      // icon-and-text utility rows, and the owner's verdict on them stands —
      // a screen that celebrates one sensor and files the other under
      // bullet points is not one product.
      //
      // The right-hand figure is PRESSURE, because it is the reading this
      // module carries that the other one cannot, and the header's arrow is
      // its tendency. Gas appears only as its departure hairline, in the band
      // the CO2 scale occupies when a CO2 module is present — same place,
      // same rule: nothing is drawn while the air is where it has been.
      drawGasBand(painter, snapshot, theme, kBandBarTop);
      drawAirAlert(painter, snapshot, theme);
      drawGasCalibrationNotice(painter, snapshot, theme);
      constexpr int kDigitWidth = 22;
      constexpr int kDigitGap = 4;
      constexpr int kDecimalPoint = 11;
      constexpr int kPressureRight = kHomeMargin + kHomeContent - 1;
      constexpr int kTemperatureRight =
          kHomeMargin + (3 * kDigitWidth + 2 * kDigitGap + kDecimalPoint) - 1;
      drawSegmentDecimal(painter.framebuffer, kTemperatureRight, kBandDigitsTop,
                         kDigitWidth, kBandDigitsHeight, 6, kDigitGap,
                         static_cast<int>(snapshot.sensor_temperature_centi_celsius),
                         theme.text);
      painter.text(kHomeMargin, kBandUnitsTop, "°C", FontSize::caption,
                   theme.muted, 60);
      if (snapshot.sensor_pressure_centi_hectopascal != kSensorAbsent) {
        drawSegmentNumber(painter.framebuffer, kPressureRight, kBandDigitsTop,
                          kDigitWidth, kBandDigitsHeight, 6, kDigitGap,
                          static_cast<int>(
                              snapshot.sensor_pressure_centi_hectopascal / 100),
                          theme.brand);
        painter.text(kPressureRight - measureText("hPa", FontSize::caption) + 1,
                     kBandUnitsTop, "hPa", FontSize::caption, theme.muted, 60);
      }
      drawHumidityFloor(painter, snapshot, theme);
    } else {
      // Nothing attached. The programme already has its band, so the space below
      // becomes the clock at a size that reads from a doorway — a cube with no
      // module is a clock most of the time anybody looks at it.
      const int cardTop = kBandBarTop;
      const int cardHeight = kBandUnitsTop + kBandUnitsHeight - cardTop;
      painter.centered(160, centredIn(cardTop, cardHeight, kAscentDisplay),
                       time.data(), FontSize::display, theme.text, kHomeContent);
    }
    return;
  }

  // The full radio home. Reworked to the owner's brief of 2026-07-30: volume
  // shown properly and at the TOP LEFT, the menu moved up to the right corner,
  // and the "playing reliably" chip removed.
  //
  // That chip is worth a note, because deleting a status indicator usually
  // deserves an argument. It said "Gra stabilnie" every second of every day the
  // radio worked, which is almost all of them — so it carried no information
  // while occupying a 166 by 24 strip under the metadata. What it was actually
  // for is still there: the degraded state now colours the volume ring, so the
  // screen says something only when there is something to say.
  //
  //   clock+date at the margin | volume ring about 103..145 | menu 268..311
  // The logo gives up 24 pixels so the programme can have them. A 120-pixel
  // hero left the metadata a 142-pixel column, and a 142-pixel column cannot
  // hold "Hey - Zazdrosc" on one line at any weight worth calling a headline —
  // which is the whole of what the owner asked to see better.
  drawStationBadge(painter, theme, station, {kHomeMargin, 66, 96, 96},
                   snapshot.station_index);
  fillRoundRect(painter.framebuffer, {268, kBandHeaderTop, 44, 44}, kHomeRadius,
                theme.raised);
  drawIconLit(painter.framebuffer, 282, kBandHeaderTop + 14, generated::kIconMenu2,
              theme.brand, theme.canvas);

  // Volume as the same closing ring the owner asked for on the minimal home,
  // top left, with the figure inside it. One control, one shape, wherever it
  // appears. Amber while the stream is recovering — which is the whole of what
  // the removed chip used to say, in a place the eye is already looking.
  // Clock and date open the row at the left margin, the volume ring follows —
  // swapped 2026-07-30 by owner instruction, and the better order: the row now
  // reads time first on every home screen, and the ring sits beside the menu
  // half of the header the way the arrow does on the other two.
  {
    std::array<char, 8> time{};
    std::array<char, 20> date{};
    formatClock(snapshot, time, date);
    constexpr int kClockStack = kAscentDisplay + 4 + kAscentCaption;
    constexpr int kClockTop = centredIn(kBandHeaderTop, 44, kClockStack);
    painter.text(kHomeMargin, kClockTop, time.data(), FontSize::display,
                 theme.text, 110);
    painter.text(kHomeMargin, kClockTop + kAscentDisplay + 4, date.data(),
                 FontSize::caption, theme.muted, 110);
  }
  {
    // The ring is gone. The owner read it as foreign to this GUI
    // (2026-07-30 night: "kółko obok zegarka nie pasuje"), and he was
    // right — the closing ring was never this product's instrument, the
    // tuning scale is. So volume wears the same miniature scale the volume
    // screen now draws full-size: ticks, a needle, the figure after it.
    // Still snug after the clock, still inside the hit-box in
    // ui_controller.hpp / device-flow.mjs, which moved to 104..232 with it.
    // One brand ink for the chrome, not nine station inks — the owner's
    // second ruling of the night. The station's own colour keeps to the
    // places that mean identity: the tile ring and the screensavers.
    const std::uint16_t ink =
        snapshot.degraded ? theme.warning : theme.brand;
    constexpr int kScaleLeft = 112;
    constexpr int kScaleWidth = 82;
    constexpr int kScaleBase = kBandHeaderTop + 36;
    for (int index = 0; index <= 10; ++index) {
      const int x = kScaleLeft + index * kScaleWidth / 10;
      const bool major = (index % 5) == 0;
      const int height = major ? 14 : 8;
      fillRoundRect(painter.framebuffer,
                    {x - 1, kScaleBase - height, 2, height}, 1,
                    major ? theme.text : theme.muted);
    }
    fillRoundRect(painter.framebuffer, {kScaleLeft, kScaleBase, kScaleWidth, 2},
                  1, theme.surface);
    const int pos = kScaleLeft + snapshot.volume * kScaleWidth / 100;
    fillRoundRect(painter.framebuffer,
                  {kScaleLeft, kScaleBase, pos - kScaleLeft, 2}, 1, ink);
    fillRoundRect(painter.framebuffer, {pos - 1, kScaleBase - 22, 2, 26}, 1,
                  ink);
    std::array<char, 8> volume{};
    std::snprintf(volume.data(), volume.size(), "%u", snapshot.volume);
    painter.text(kScaleLeft + kScaleWidth + 10,
                 kBandHeaderTop + 28 - kAscentBody / 2, volume.data(),
                 FontSize::body, theme.text, 30);
    // The battery joins the header (docs/137 Q1, the owner's "i aku"): a
    // plain cell before the menu, station accent for the charge, amber once
    // it falls to a quarter — at this size the hue IS the numeral. A mint
    // dot past the nub means external power. Deliberately outside the
    // volume hit-box; the cell reports, it does not invite.
    if (snapshot.battery_available) {
      constexpr int kBattX = 232;
      constexpr int kBattY = kBandHeaderTop + 23;
      fillRoundRect(painter.framebuffer, {kBattX, kBattY, 22, 11}, 3,
                    theme.muted);
      fillRoundRect(painter.framebuffer, {kBattX + 1, kBattY + 1, 20, 9}, 2,
                    theme.canvas);
      const int fill = snapshot.battery_level_percent * 16 / 100;
      fillRoundRect(painter.framebuffer, {kBattX + 3, kBattY + 3, fill, 5}, 1,
                    snapshot.battery_level_percent <= 25 ? theme.warning
                                                         : theme.brand);
      fillRect(painter.framebuffer, {kBattX + 22, kBattY + 3, 2, 5},
               theme.muted);
      if (snapshot.battery_external_power) {
        fillCircle(painter.framebuffer, kBattX + 28, kBattY + 5, 2,
                   theme.brand);
      }
    }
  }
  // Long station names get the smaller weight so the full name shows instead
  // of an ellipsis; short names keep the title weight.
  // What is playing, given the room the removed chip freed: a taller card, the
  // heading in its own colour above it, and the title at TITLE weight rather
  // than body. This is the reason somebody chose this screen over the minimal
  // one, and it was set two sizes smaller than the station name above it.
  constexpr int kCardLeft = 116;
  constexpr int kCardWidth = kScreenWidth - kHomeMargin - kCardLeft;
  constexpr int kCardTextLeft = kCardLeft + 12;
  constexpr int kCardTextWidth = kCardWidth - 24;
  fillRoundRect(painter.framebuffer, {kCardLeft, 66, kCardWidth, 96}, kHomeRadius,
                theme.surface);
  painter.text(kCardTextLeft, 76, translated(snapshot, "Teraz gra", "Now playing"),
               FontSize::caption, theme.muted, kCardTextWidth);
  {
    // Title weight while the whole thing fits in two lines, body weight when it
    // does not. The bound is measured against the wrapped width rather than
    // guessed, because the point of the change is that the programme is READ,
    // and an ellipsis undoes it.
    const char* value = metadataTitle(snapshot);
    const bool useTitle =
        measureText(value, FontSize::title) <= kCardTextWidth * 2;
    const FontSize size = useTitle ? FontSize::title : FontSize::body;
    const int advance = useTitle ? kAscentTitle + 6 : kLineAdvanceBody;
    const int cap = useTitle ? kCapBody + 6 : kCapBody;
    const int measured = measureText(value, size);
    const int lines = measured <= kCardTextWidth               ? 1
                      : measured <= kCardTextWidth * 2          ? 2
                                                                : 3;
    const int capBlock = cap + (lines - 1) * advance;
    drawWrappedText(painter, kCardTextLeft, centredIn(96, 62, capBlock) - kInkTopBody,
                    value, size, theme.text, kCardTextWidth, 3);
  }
  drawTransport(painter, snapshot, theme);
}

void drawSettingsGrid(Painter& painter, const CompactSnapshot& snapshot,
                      const ThemeTokens& theme,
                      const std::array<const char*, 6>& labels,
                      const std::array<const char*, 6>& values) {
  for (std::size_t index = 0; index < labels.size(); ++index) {
    if (labels[index] == nullptr) continue;
    const int column = static_cast<int>(index % 2);
    const int row = static_cast<int>(index / 2);
    const int left = 8 + column * 156;
    const int top = 48 + row * 44;
    const bool destructive =
        snapshot.screen == ScreenKind::settings_system && index == 4;
    fillRoundRect(painter.framebuffer, {left, top + 2, 148, 40}, 11,
                  destructive && snapshot.confirm_delete ? theme.danger
                                                          : theme.surface);
    painter.text(left + 10, top + 5, labels[index], FontSize::caption,
                 destructive && snapshot.confirm_delete ? theme.canvas
                                                         : theme.muted,
                 104);
    painter.text(left + 10, top + 21, values[index], FontSize::caption,
                 destructive && snapshot.confirm_delete ? theme.canvas
                                                         : theme.text,
                 104);
    drawIconMask24(painter.framebuffer, left + 116, top + 10,
                   generated::kIconChevronRight,
                   destructive && snapshot.confirm_delete ? theme.canvas
                                                           : theme.muted);
  }
  const std::array<const char*, 3> pages{
      translated(snapshot, "Szybkie", "Quick"),
      translated(snapshot, "System", "System"),
      translated(snapshot, "Ekran", "Display")};
  const int active = snapshot.screen == ScreenKind::settings_quick
                         ? 0
                         : snapshot.screen == ScreenKind::settings_system ? 1 : 2;
  for (int index = 0; index < 3; ++index) {
    const int left = 8 + index * 104;
    fillRoundRect(painter.framebuffer, {left, 190, 96, 44}, 13,
                  index == active ? theme.brand : theme.raised);
    painter.centered(left + 48, 202, pages[static_cast<std::size_t>(index)],
                     FontSize::caption,
                     index == active ? theme.on_brand : theme.text, 82);
  }
}

const char* screenSaverName(const CompactSnapshot& snapshot) {
  switch (snapshot.screensaver_mode) {
    case 1: return "Bars";
    case 2: return "Orbit";
    case 3: return translated(snapshot, "Kot", "Cat");
    default: return "Pulse";
  }
}

void drawSettings(Painter& painter, const CompactSnapshot& snapshot,
                  const ThemeTokens& theme) {
  fill(painter.framebuffer, theme.canvas);
  std::array<char, 40> battery_primary{};
  std::array<char, 48> battery_secondary{};
  if (!snapshot.battery_available) {
    std::snprintf(battery_primary.data(), battery_primary.size(), "%s --",
                  translated(snapshot, "AKU", "BAT"));
    std::snprintf(battery_secondary.data(), battery_secondary.size(), "%s",
                  translated(snapshot, "Brak odczytu", "No reading"));
  } else {
    const auto battery_current =
        static_cast<std::int32_t>(snapshot.battery_current_milliamps);
    const auto battery_current_magnitude = static_cast<unsigned>(
        battery_current < 0 ? -battery_current : battery_current);
    const auto voltage_whole = snapshot.battery_voltage_millivolts / 1000U;
    const auto voltage_fraction =
        (snapshot.battery_voltage_millivolts % 1000U) / 10U;
    std::snprintf(battery_primary.data(), battery_primary.size(),
                  "%s %u%% | %u.%02u V", translated(snapshot, "AKU", "BAT"),
                  static_cast<unsigned>(snapshot.battery_level_percent),
                  static_cast<unsigned>(voltage_whole),
                  static_cast<unsigned>(voltage_fraction));
    if (snapshot.battery_external_power) {
      std::snprintf(
          battery_secondary.data(), battery_secondary.size(), "USB | %s %ld mA",
          snapshot.battery_current_milliamps > 10
              ? translated(snapshot, "ładowanie", "charging")
              : translated(snapshot, "podtrzymanie", "external power"),
          static_cast<long>(battery_current));
    } else if (snapshot.battery_runtime_valid) {
      const auto hours = snapshot.battery_runtime_minutes / 60U;
      const auto minutes = snapshot.battery_runtime_minutes % 60U;
      std::snprintf(
          battery_secondary.data(), battery_secondary.size(), "%s %u mA | ~%u h %02u min",
          translated(snapshot, "Pobór", "Draw"),
          battery_current_magnitude,
          static_cast<unsigned>(hours), static_cast<unsigned>(minutes));
    } else {
      std::snprintf(
          battery_secondary.data(), battery_secondary.size(), "%s %u mA | %s",
          translated(snapshot, "Pobór", "Draw"),
          battery_current_magnitude,
          translated(snapshot, "pomiar czasu", "estimating"));
    }
  }
  // 152, not 196: the signal bars begin at x=166 and text allowed to run to 204
  // would have written through them.
  painter.text(kHomeMargin, 3, battery_primary.data(), FontSize::body, theme.text, 152);
  painter.text(kHomeMargin, 23, battery_secondary.data(), FontSize::caption, theme.muted,
               152);
  // Noise is the only application shortcut here. A "CO2" button sat beside it
  // until 2026-07-29 and was removed by owner decision: the air belongs on the
  // HOME screens, not behind a settings icon. Somebody does not go looking for
  // the air in a menu — they see it while looking at something else, or they
  // never see it.
  // The signal, beside the noise button, by owner decision of 2026-07-30. It
  // came off the home screens because it is the state of a CONNECTION and not
  // of the programme or the air; it lands here because this is the screen
  // somebody is already on when they wonder about the network — the Wi-Fi tile
  // that opens the detail is two rows below it.
  //
  // Not a button. It sits outside the 44-pixel chips deliberately, because the
  // one thing worse than a control that is hard to press is a thing that looks
  // like a control and does nothing.
  constexpr int kSignalLeft = 166;
  constexpr int kSignalBaseline = 36;
  for (int bar = 0; bar < 4; ++bar) {
    const int height = 6 + bar * 4;
    fillRoundRect(painter.framebuffer,
                  {kSignalLeft + bar * 10, kSignalBaseline - height, 7, height}, 2,
                  snapshot.wifi_online && snapshot.wifi_strength_percent > bar * 25
                      ? theme.text
                      : theme.border);
  }
  fillRoundRect(painter.framebuffer, {216, 2, 44, 44}, 14, theme.raised);
  drawNoiseGlyph(painter.framebuffer, 238, 24, theme.text, theme.raised);
  fillRoundRect(painter.framebuffer, {268, 2, 44, 44}, 14, theme.raised);
  drawIconMask24(painter.framebuffer, 278, 12, generated::kIconX, theme.text);
  std::array<const char*, 6> labels{};
  std::array<const char*, 6> values{};
  std::array<char, 8> volume{};
  std::array<char, 8> brightness{};
  std::array<char, 12> saver_delay{};
  std::array<char, 12> off_delay{};
  std::snprintf(volume.data(), volume.size(), "%u%%", snapshot.volume);
  std::snprintf(brightness.data(), brightness.size(), "%u%%", snapshot.brightness);
  std::snprintf(saver_delay.data(), saver_delay.size(), "%u min",
                snapshot.screensaver_delay_seconds / 60);
  std::snprintf(off_delay.data(), off_delay.size(), "%u min",
                snapshot.display_off_delay_seconds / 60);
  if (snapshot.screen == ScreenKind::settings_quick) {
    labels = {"Wi-Fi", "Bluetooth", translated(snapshot, "Głośność", "Volume"),
              translated(snapshot, "Jasność", "Brightness"),
              translated(snapshot, "Motyw", "Theme"),
              translated(snapshot, "Język", "Language")};
    values = {snapshot.wifi_online ? translated(snapshot, "Połączono", "Connected")
                                   : translated(snapshot, "Brak sieci", "Offline"),
              snapshot.output == AudioOutput::bluetooth ? "A2DP" : "Radio",
              volume.data(), brightness.data(),
              snapshot.theme == ThemeMode::dark ? translated(snapshot, "Ciemny", "Dark")
                                                : translated(snapshot, "Jasny", "Light"),
              snapshot.locale == LocaleMode::pl ? "Polski" : "English"};
  } else if (snapshot.screen == ScreenKind::settings_system) {
    labels = {translated(snapshot, "Ekran główny", "Home screen"),
              translated(snapshot, "Wi-Fi i sieci", "Wi-Fi & networks"),
              translated(snapshot, "O projekcie", "About"),
              translated(snapshot, "Diagnostyka", "Diagnostics"),
              snapshot.confirm_delete
                  ? translated(snapshot, "Potwierdź", "Confirm")
                  : translated(snapshot, "Wyczyść dane", "Erase data"),
              translated(snapshot, "Konsola", "Console")};
    // Without a module there is only one home to choose from, and the tile says
    // so instead of being a control that does nothing when pressed. The two air
    // homes are about the air; with nothing measuring, they are a clock over an
    // empty half-screen, and somebody who has not bought a sensor should never
    // be able to open one.
    values = {modulesOk(snapshot) == 0
                  ? translated(snapshot, "Tylko pełny", "Full only")
              : snapshot.variant == HomeVariant::editorial
                  ? translated(snapshot, "Pełny", "Full")
              : snapshot.variant == HomeVariant::glance
                  ? translated(snapshot, "Minimalny", "Minimal")
                  : translated(snapshot, "Powietrze", "Air"),
              translated(snapshot, "Zmień", "Change"),
              "Open Radio", "Pro",
              snapshot.confirm_delete ? "RESET"
                                      : translated(snapshot, "Wszystko", "All"),
              snapshot.console_active
                  ? translated(snapshot, "Zamknij", "Close")
                  : translated(snapshot, "Otwórz", "Open")};
  } else {
    labels = {translated(snapshot, "Wygaszacz", "Screensaver"),
              translated(snapshot, "Start po", "Starts after"),
              translated(snapshot, "Tryb", "Mode"),
              translated(snapshot, "Ekran OFF", "Screen off"),
              translated(snapshot, "Wyłącz po", "Turns off"),
              translated(snapshot, "Podgląd", "Preview")};
    values = {snapshot.screensaver_enabled ? translated(snapshot, "Włączony", "On")
                                           : translated(snapshot, "Wyłączony", "Off"),
              saver_delay.data(), screenSaverName(snapshot),
              snapshot.display_off_enabled ? translated(snapshot, "Włączony", "On")
                                           : translated(snapshot, "Wyłączony", "Off"),
              off_delay.data(), translated(snapshot, "Otwórz", "Open")};
  }
  drawSettingsGrid(painter, snapshot, theme, labels, values);
}

void drawNoise(Painter& painter, const CompactSnapshot& snapshot,
               const ThemeTokens& theme) {
  fill(painter.framebuffer, theme.canvas);
  painter.text(kHomeMargin, 8, translated(snapshot, "Szum", "Noise"), FontSize::title,
               theme.text, 150);
  std::array<char, 8> time{};
  std::array<char, 20> date{};
  formatClock(snapshot, time, date);
  painter.text(206, 13, time.data(), FontSize::caption, theme.muted, 52);
  fillRoundRect(painter.framebuffer, {268, 2, 44, 44}, 14, theme.raised);
  drawIconMask24(painter.framebuffer, 278, 12, generated::kIconX, theme.text);

  const auto controlColor = snapshot.noise_playing ? theme.brand : theme.raised;
  const auto controlForeground =
      snapshot.noise_playing ? theme.on_brand : theme.text;
  fillCircle(painter.framebuffer, 160, 102, 52, controlColor);
  if (snapshot.noise_playing) {
    fillRoundRect(painter.framebuffer, {146, 80, 28, 28}, 5,
                  controlForeground);
  } else {
    for (int row = 0; row < 30; ++row) {
      const int half = row < 15 ? row / 2 : (29 - row) / 2;
      fillRect(painter.framebuffer, {148, 79 + row, 10 + half, 1},
               controlForeground);
    }
  }
  painter.centered(160, 122,
                   snapshot.noise_playing ? "STOP" : "PLAY",
                   FontSize::caption, controlForeground, 72);

  const std::array<const char*, 3> polish{{"BIAŁY", "RÓŻOWY", "BRĄZOWY"}};
  const std::array<const char*, 3> english{{"WHITE", "PINK", "BROWN"}};
  for (int index = 0; index < 3; ++index) {
    const int left = 8 + index * 104;
    const bool selected = snapshot.noise_color == index;
    fillRoundRect(painter.framebuffer, {left, 166, 96, 40}, 14,
                  selected ? theme.brand : theme.surface);
    painter.centered(left + 48, 178,
                     translated(snapshot, polish[static_cast<std::size_t>(index)],
                                english[static_cast<std::size_t>(index)]),
                     FontSize::caption,
                     selected ? theme.on_brand : theme.text, 82);
  }
}

// Environment readings. Deliberately a screen and not a third audio mode: Radio
// and Noise are exclusive because both drive the speaker, and readings drive
// nothing, so looking at the air must never stop the music.
//
// What this screen may and may not say is a contract, not a style choice
// (release/claims-vocabulary.json). It shows quantities and their age. It does
// not grade them, does not advise, and never carries the words this project
// forbade itself: no air quality, no index, no "open a window". A reading is
// informational, and the surest way to keep that true is to give the screen no
// vocabulary for anything else.
//
// Units are written for the font atlas that exists: no subscript two and no
// ohm sign, so CO2 and kOhm. The degree sign is present and used.
// The minimal home: what is playing, said with a logo, and the air, said with
// as much of the screen as is left.
//
// The owner's brief, and every line of it is a removal. No station name in
// text — the logo IS the name, and a person who chose that tile knows it. No
// "playing over Bluetooth" caption — the route is a state, not news. The volume
// and network widgets shrink to marks rather than panels. What that buys is
// room, and the room goes to the readings.
//
// The layout answers three cases, because a person may own no module, one, or
// two, and a screen that assumes the wrong one is broken for most people:
//   none  — the air area holds the clock, large. Nothing is missing, because
//           nothing was promised.
//   one   — an SCD41 gives CO2, temperature, humidity: one headline and two marks.
//   two   — pressure and the gas trend join, and the row splits four ways.
//
// Distance matters more than density here. At three metres a pixel of this
// panel subtends 8.7 arcseconds against an eye that resolves about a minute, so
// the digits are gone and only area, colour and position survive. The CO2 bar
// spans the full width for exactly that reason: it is the one element that says
// something across a room.
void drawNowPlayingAir(Painter& painter, const CompactSnapshot& snapshot,
                       const ThemeTokens& theme, const StationTheme* stations,
                       std::size_t station_count) {
  fill(painter.framebuffer, theme.canvas);
  // The air home, rebuilt on the minimal home's bands on 2026-07-30.
  //
  // It had its own everything: a 36-pixel badge at y=4 against 56 at y=8, a
  // title-size clock at 54 against a display-size one at 72, signal bars, a
  // volume ring, a menu button at y=0, then values at 96, 100, 154, 156, 158,
  // 174, 196 and 208. Every number was arrived at honestly and the screen did
  // not hold together, which is the same failure the minimal home had before it
  // was cut into bands — and fixing it twice by nudging would have produced two
  // screens that disagree in a third way.
  //
  // So the skeleton is now shared and only the CONTENTS differ. Minimal answers
  // "what is playing, and by the way, the air"; this answers "what is the air
  // doing, and by the way, the radio is on". The one band that differs is the
  // second: minimal puts the track there, and this puts what the air is doing —
  // its rate, its projection, and the dew point, which is the number that turns
  // two readings into something a person can act on.
  const StationTheme* station =
      stations != nullptr && snapshot.station_index < station_count
          ? &stations[snapshot.station_index]
          : nullptr;
  const StationTheme kFallbackStation{"", "", "", theme.brand, theme.brand,
                                    theme.on_brand};
  std::array<char, 8> time{};
  std::array<char, 20> date{};
  formatClock(snapshot, time, date);
  drawHomeHeader(painter, snapshot, theme,
                 station != nullptr ? *station : kFallbackStation, time, date);

  if (modulesOk(snapshot) == 0) {
    // Nothing measuring. The panel becomes the clock rather than a row of
    // dashes apologising for hardware nobody bought. This screen is not
    // reachable without a module any more (ui_controller::homeVariant), so this
    // is the moment a module is unplugged while it is open.
    painter.centered(160, centredIn(kBandTitleTop, 120, kAscentDisplay),
                     time.data(), FontSize::display, theme.text, kHomeContent);
    return;
  }

  // BAND 2 — what the air is DOING, in the card the track title occupies on the
  // minimal home. Two columns on the same grid: the dew point on the left, the
  // rate and projection on the right.
  fillRoundRect(painter.framebuffer,
                {kHomeMargin, kBandTitleTop, kHomeContent, kBandTitleHeight},
                kHomeRadius, theme.surface);
  {
    const int labelTop = kBandTitleTop + 6;
    const int valueTop = kBandTitleTop + 20;
    std::array<char, 24> dew{};
    if (snapshot.sensor_temperature_centi_celsius != kSensorAbsent &&
        snapshot.sensor_humidity_centi_percent != kSensorAbsent) {
      // Magnus, the same formula the firmware uses, because a screen that
      // recomputed it differently would be a second answer to one question.
      // Carried as a rendered figure rather than re-derived: this is the
      // drawing layer.
      const std::int32_t dewCenti = snapshot.sensor_dew_point_centi_celsius;
      if (dewCenti != kSensorAbsent) {
        const long whole = static_cast<long>(dewCenti / 100);
        const long fraction = static_cast<long>((dewCenti / 10) % 10);
        std::snprintf(dew.data(), dew.size(), "%ld.%01ld °C", whole,
                      fraction < 0 ? -fraction : fraction);
      }
    }
    painter.text(kHomeMargin + 12, labelTop,
                 translated(snapshot, "Punkt rosy", "Dew point"),
                 FontSize::caption, theme.muted, kHomeColumn - 12);
    painter.text(kHomeMargin + 12, valueTop, dew[0] != '\0' ? dew.data() : "--",
                 FontSize::body, theme.text, kHomeColumn - 12);

    std::array<char, 32> trend{};
    const bool atCeiling = snapshot.sensor_co2_ceiling_ppm != kSensorAbsent &&
                           snapshot.sensor_co2_ppm != kSensorAbsent &&
                           snapshot.sensor_co2_ppm >= snapshot.sensor_co2_ceiling_ppm;
    if (atCeiling) {
      std::snprintf(trend.data(), trend.size(), "%s",
                    translated(snapshot, "koniec skali", "off scale"));
    } else if (snapshot.sensor_co2_minutes_to_next != kSensorAbsent &&
               snapshot.sensor_co2_next_threshold_ppm != kSensorAbsent) {
      // The projection, which is the one thing on this cube that arrives BEFORE
      // the state it describes.
      std::snprintf(trend.data(), trend.size(), "%ld %s %ld min",
                    static_cast<long>(snapshot.sensor_co2_next_threshold_ppm),
                    translated(snapshot, "za", "in"),
                    static_cast<long>(snapshot.sensor_co2_minutes_to_next));
    } else if (snapshot.sensor_co2_rate_ppm_per_hour != kSensorAbsent) {
      const long rate = static_cast<long>(snapshot.sensor_co2_rate_ppm_per_hour);
      std::snprintf(trend.data(), trend.size(), "%s%ld ppm/h", rate > 0 ? "+" : "",
                    rate);
    } else if (snapshot.sensor_pressure_change_centi_hpa_3h != kSensorAbsent) {
      // No CO2 on the wire: the tendency column belongs to the barometer. The
      // header's arrow gives the direction; this gives the number behind it.
      const long change = static_cast<long>(snapshot.sensor_pressure_change_centi_hpa_3h);
      const long magnitude = change < 0 ? -change : change;
      std::snprintf(trend.data(), trend.size(), "%s%ld.%01ld hPa/3h",
                    change > 0 ? "+" : change < 0 ? "-" : "",
                    magnitude / 100, (magnitude / 10) % 10);
    }
    painter.text(kHomeColumnTwo, labelTop,
                 translated(snapshot, "Tendencja", "Trend"), FontSize::caption,
                 theme.muted, kHomeColumn - 12);
    painter.text(kHomeColumnTwo, valueTop, trend[0] != '\0' ? trend.data() : "--",
                 FontSize::body, theme.text, kHomeColumn - 12);
  }

  if (snapshot.sensor_co2_ppm != kSensorAbsent) {
    // The CO2 composition, shared with the minimal home.
    drawAirScale(painter, snapshot, theme);
    drawAirAlert(painter, snapshot, theme);
    drawGasCalibrationNotice(painter, snapshot, theme);
    drawAirFigures(painter, snapshot, theme);
    drawHumidityFloor(painter, snapshot, theme);
    return;
  }

  // ENV Pro alone: the weather face. Everything the CO2 layout gave to ppm
  // goes to PRESSURE — the hero figure at the old headline size, the
  // temperature beside it in the same family, the gas departure as the one
  // moving line, the humidity as the floor. The owner's finding that forced
  // this branch into existence: without it, a cube wearing only this module
  // opened its weather screen onto a vacated middle.
  drawAirAlert(painter, snapshot, theme);
  drawGasCalibrationNotice(painter, snapshot, theme);
  if (snapshot.sensor_temperature_centi_celsius != kSensorAbsent) {
    drawSegmentDecimal(painter.framebuffer, 124, 152, 24, 48, 7, 5,
                       static_cast<int>(snapshot.sensor_temperature_centi_celsius),
                       theme.text);
    painter.text(kHomeMargin, 206, "°C", FontSize::caption, theme.muted, 40);
  }
  if (snapshot.sensor_pressure_centi_hectopascal != kSensorAbsent) {
    drawSegmentNumber(painter.framebuffer, 311, 144, 28, 56, 7, 5,
                      static_cast<int>(
                          snapshot.sensor_pressure_centi_hectopascal / 100),
                      theme.brand);
    painter.text(311 - measureText("hPa", FontSize::caption) + 1, 206, "hPa",
                 FontSize::caption, theme.muted, 48);
  }
  drawGasBand(painter, snapshot, theme, kBandBarTop);
  drawHumidityFloor(painter, snapshot, theme);
}

void drawSensors(Painter& painter, const CompactSnapshot& snapshot,
                 const ThemeTokens& theme) {
  fill(painter.framebuffer, theme.canvas);
  painter.text(kHomeMargin, 8, translated(snapshot, "Czujniki", "Sensors"),
               FontSize::title, theme.text, 190);
  std::array<char, 8> time{};
  std::array<char, 20> date{};
  formatClock(snapshot, time, date);
  painter.text(206, 13, time.data(), FontSize::caption, theme.muted, 52);
  fillRoundRect(painter.framebuffer, {268, 2, 44, 44}, 14, theme.raised);
  drawIconMask24(painter.framebuffer, 278, 12, generated::kIconX, theme.text);

  bool anyModule = false;
  for (const auto status : snapshot.sensor_status) {
    if (status != 0) anyModule = true;
  }
  if (!anyModule) {
    fillRoundRect(painter.framebuffer, {8, 60, 304, 104}, 16, theme.surface);
    painter.centered(160, 92,
                     translated(snapshot, "Brak modułu", "No module"),
                     FontSize::body, theme.text, 280);
    painter.centered(160, 120,
                     translated(snapshot, "Podłącz do portu A",
                                "Connect one to port A"),
                     FontSize::caption, theme.muted, 280);
    return;
  }

  // The large number is CO2 when a module measures it, because that is the one
  // people watch. When none does — a lone ENV Pro — the space goes to
  // temperature rather than to a dash pretending a value is missing.
  const bool hasCo2 = snapshot.sensor_co2_ppm != kSensorAbsent;
  fillRoundRect(painter.framebuffer, {8, 52, 304, 84}, 16, theme.surface);
  std::array<char, 16> headline{};
  if (hasCo2) {
    const bool atCeiling = snapshot.sensor_co2_ceiling_ppm != kSensorAbsent &&
                           snapshot.sensor_co2_ppm >= snapshot.sensor_co2_ceiling_ppm;
    std::snprintf(headline.data(), headline.size(), "%ld%s",
                  static_cast<long>(snapshot.sensor_co2_ppm),
                  atCeiling ? "+" : "");
    painter.text(20, 62, headline.data(), FontSize::title, theme.text, 200);

    // The same band colour the home screen uses, on a slim scale inside the
    // card. This screen is read close up, so it does not need the whole panel
    // to be the signal — but it must not CONTRADICT the home screen either, and
    // two screens showing the same reading in different colours would.
    const std::uint16_t bandColour =
        snapshot.sensor_air_band >= 4   ? theme.danger
        : snapshot.sensor_air_band == 3 ? theme.warning
        : snapshot.sensor_air_band == 0 ? theme.muted
                                        : theme.success;
    const std::int32_t clamped = snapshot.sensor_co2_ppm < 400 ? 400
                                 : snapshot.sensor_co2_ppm > 2000
                                     ? 2000
                                     : snapshot.sensor_co2_ppm;
    const int scaleWidth = static_cast<int>((clamped - 400) * 176 / 1600);
    fillRoundRect(painter.framebuffer, {20, 92, 176, 6}, 3, theme.raised);
    if (scaleWidth > 0) {
      fillRoundRect(painter.framebuffer, {20, 92, scaleWidth < 4 ? 4 : scaleWidth, 6},
                    3, bandColour);
    }

    // The instruction, when there is one to give. Same wording as the home
    // screen, for the same reason.
    if (snapshot.sensor_air_band >= 3) {
      const char* instruction =
          snapshot.sensor_air_band >= 5
              ? translated(snapshot, "Wywietrz teraz", "Air it now")
              : translated(snapshot, "Wywietrz pokój", "Air the room");
      // The limit is what is left of the card, not a guessed constant: at body
      // size "Wywietrz pokój" is about 112 px and a 110 px cap ellipsised it
      // into "Wywietrz poko…", which the truncation gate then reported.
      const int width = measureText(instruction, FontSize::body);
      painter.text(300 - width, 64, instruction, FontSize::body,
                   snapshot.sensor_air_band >= 4 ? theme.danger : theme.warning,
                   300 - 20 - measureText(headline.data(), FontSize::title) - 12);
    }

    drawIconLit(painter.framebuffer, 20, 100, generated::kIconAtom, theme.muted,
                theme.canvas);
    painter.text(48, 104, "CO2 ppm", FontSize::caption, theme.muted, 100);
  } else if (snapshot.sensor_temperature_centi_celsius != kSensorAbsent) {
    std::snprintf(headline.data(), headline.size(), "%ld.%01ld°",
                  static_cast<long>(snapshot.sensor_temperature_centi_celsius / 100),
                  static_cast<long>((snapshot.sensor_temperature_centi_celsius / 10) % 10));
    painter.text(20, 62, headline.data(), FontSize::title, theme.text, 200);
    painter.text(20, 104, translated(snapshot, "temperatura", "temperature"),
                 FontSize::caption, theme.muted, 160);
  }

  // Age, right of the headline, with the rate beside it when history supports
  // one. A number with no age silently claims to be now, and a number with no
  // direction silently claims to be still.
  std::array<char, 28> age{};
  int ageWritten = 0;
  if (snapshot.sensor_age_seconds < 90) {
    ageWritten = std::snprintf(age.data(), age.size(), "%us",
                               snapshot.sensor_age_seconds);
  } else {
    ageWritten = std::snprintf(age.data(), age.size(), "%umin",
                               snapshot.sensor_age_seconds / 60);
  }
  if (ageWritten > 0 && snapshot.sensor_co2_rate_ppm_per_hour != kSensorAbsent &&
      static_cast<std::size_t>(ageWritten) < age.size()) {
    std::snprintf(age.data() + ageWritten, age.size() - static_cast<std::size_t>(ageWritten),
                  "  %s%ld/h", snapshot.sensor_co2_rate_ppm_per_hour > 0 ? "+" : "",
                  static_cast<long>(snapshot.sensor_co2_rate_ppm_per_hour));
  }
  {
    const int width = measureText(age.data(), FontSize::caption);
    painter.text(300 - width, 104, age.data(), FontSize::caption, theme.muted, 160);
  }

  // Everything else, two to a row, each present only if something measures it.
  struct Row {
    const char* polish;
    const char* english;
    std::int32_t value;
    int divisor;
    int decimals;
    const char* unit;
    const IconMask24* icon;
  };
  const std::array<Row, 4> rows{{
      {"Temperatura", "Temperature", hasCo2 ? snapshot.sensor_temperature_centi_celsius : kSensorAbsent, 100, 1, "°C", &generated::kIconTemperature},
      {"Wilgotność", "Humidity", snapshot.sensor_humidity_centi_percent, 100, 0, "%", &generated::kIconDroplet},
      {"Ciśnienie", "Pressure", snapshot.sensor_pressure_centi_hectopascal, 100, 0, "hPa", &generated::kIconGauge},
      {"Gaz", "Gas", snapshot.sensor_gas_resistance_ohm, 1000, 1, "kOhm", &generated::kIconWind},
  }};
  int slot = 0;
  for (const auto& row : rows) {
    if (row.value == kSensorAbsent) continue;
    const int left = 8 + (slot % 2) * 156;
    const int top = 144 + (slot / 2) * 48;
    if (top > 200) break;
    fillRoundRect(painter.framebuffer, {left, top, 148, 42}, 12, theme.surface);
    drawIconLit(painter.framebuffer, left + 9, top + 9, *row.icon, theme.muted,
                theme.canvas);
    painter.text(left + 40, top + 4, translated(snapshot, row.polish, row.english),
                 FontSize::caption, theme.muted, 100);
    std::array<char, 20> value{};
    if (row.decimals == 0) {
      std::snprintf(value.data(), value.size(), "%ld %s",
                    static_cast<long>(row.value / row.divisor), row.unit);
    } else {
      const long whole = static_cast<long>(row.value / row.divisor);
      const long fraction = static_cast<long>((row.value / (row.divisor / 10)) % 10);
      std::snprintf(value.data(), value.size(), "%ld.%01ld %s", whole,
                    fraction < 0 ? -fraction : fraction, row.unit);
    }
    painter.text(left + 40, top + 20, value.data(), FontSize::body, theme.text, 100);
    ++slot;
  }

  // A module that is answering but not yet trustworthy says so, because an
  // SCD4x needs a measurement interval before its output means anything and a
  // number shown during that window is a guess wearing a unit.
  bool anyWarming = false;
  bool anyQuarantined = false;
  for (const auto status : snapshot.sensor_status) {
    if (status == 1) anyWarming = true;
    if (status == 4) anyQuarantined = true;
  }
  if (anyWarming) {
    painter.centered(160, 218, translated(snapshot, "Rozgrzewa się", "Warming up"),
                     FontSize::caption, theme.muted, 300);
  } else if (anyQuarantined) {
    painter.centered(160, 218,
                     translated(snapshot, "Moduł odłączony po awarii",
                                "Module isolated after a fault"),
                     FontSize::caption, theme.muted, 300);
  }
}

void drawStations(Painter& painter, const CompactSnapshot& snapshot,
                  const ThemeTokens& theme, const StationTheme* stations,
                  std::size_t station_count) {
  fill(painter.framebuffer, theme.canvas);
  // By owner decision the picker is nine logo tiles and a way out — no title,
  // no footer bar. The X matches the global top-right close hitbox.
  fillRoundRect(painter.framebuffer, {268, 2, 44, 44}, 14, theme.raised);
  drawIconMask24(painter.framebuffer, 278, 12, generated::kIconX, theme.text);
  // Centred 3x3 grid. The controller and the hitbox contract share the same
  // 72 px touch pitch from origin (52, 12); tiles sit 4 px inside each cell.
  constexpr int kTile = 64;
  constexpr int kTileRadius = 16;
  constexpr int kPitch = 72;
  for (std::size_t index = 0; index < station_count; ++index) {
    const int column = static_cast<int>(index % 3);
    const int row = static_cast<int>(index / 3);
    const int left = 56 + column * kPitch;
    const int top = 16 + row * kPitch;
    const auto& item = stations[index];
    const bool selected = index == snapshot.station_index;
    Rect slot{left, top, kTile, kTile};
    int slot_radius = kTileRadius;
    if (selected) {
      // Accent ring around the playing station; the logo sits inside it.
      fillRoundRect(painter.framebuffer, {left, top, kTile, kTile},
                    kTileRadius, theme.brand);
      slot = {left + 3, top + 3, kTile - 6, kTile - 6};
      slot_radius = kTileRadius - 3;
      fillRoundRect(painter.framebuffer, slot, slot_radius, theme.raised);
    } else {
      fillRoundRect(painter.framebuffer, slot, slot_radius, theme.surface);
    }
    bool drew_logo = drawRuntimeStationLogo(painter, slot, slot_radius, index);
#if defined(OPEN_RADIO_HAS_LOGO_PACK)
    if (!drew_logo) drew_logo = drawStationLogo(painter, slot, index);
#endif
    if (!drew_logo) {
      // Monogram chip. The nine per-station colours left with the owner's
      // one-ink ruling (2026-07-30 night, "bez tych różnych 9") — the grid
      // is night surface with cream monograms, and the playing station is
      // the only one wearing the brand: a mint ring and a mint letter.
      if (!selected) fillRoundRect(painter.framebuffer, slot, slot_radius, theme.surface);
      std::array<char, 5> initial{};
      copyFirstGlyph(item.short_name, initial.data(), initial.size());
      painter.centered(left + kTile / 2,
                       top + kTile / 2 -
                           generated::interFace(FontSize::display).ascent / 2,
                       initial.data(), FontSize::display,
                       selected ? theme.brand : theme.text, kTile);
    }
  }
}




void drawCat(Painter& painter, const CompactSnapshot& snapshot,
             const ThemeTokens& theme, const StationTheme& station) {
  constexpr std::array<std::uint16_t, 4> palette{{
      rgb565(38, 42, 47), rgb565(92, 100, 109), rgb565(151, 160, 170),
      rgb565(218, 224, 229)}};
  constexpr int scale = 3;
  const int bob = snapshot.animation_frame % 16 < 8 ? 0 : 1;
  const int origin_x = (kScreenWidth - generated::kKiaraWidth * scale) / 2;
  const int origin_y = 38 + bob;
  fillRoundRect(painter.framebuffer, {58, 38, 204, 170}, 28, theme.surface);
  fillRoundRect(painter.framebuffer, {62, 42, 196, 162}, 24, theme.canvas);
  for (const auto& run : generated::kKiaraRuns) {
    fillRect(painter.framebuffer,
             {origin_x + run.x * scale, origin_y + run.y * scale,
              run.length * scale, scale},
             palette[run.color]);
  }
  painter.text(kHomeMargin, 10, "Kiara", FontSize::title, theme.text, 220);
  painter.centered(160, 218, metadataTitle(snapshot), FontSize::caption,
                   theme.muted, 286);
  fillRoundRect(painter.framebuffer, {12, 36, 44, 5}, 2, station.accent);
}

// The percent screens were the last ones wearing the default blue: a filled
// slider with a white ball. The owner picked the tuning scale over a grown-up
// ring and over a re-inked slider (2026-07-30 night, three rendered variants)
// — a scale with a needle is this product's own instrument, and the tick
// geometry keeps the exact 24+272 span percentAt() has always mapped, so the
// controller does not move. Station accent, amber while degraded, like every
// other live reading.
void drawPercentControl(Painter& painter, const CompactSnapshot& snapshot,
                        const ThemeTokens& theme,
                        const char* title, std::uint8_t value) {
  fill(painter.framebuffer, theme.canvas);
  drawUtilityHeader(painter, theme, title);
  std::array<char, 8> percentage{};
  std::snprintf(percentage.data(), percentage.size(), "%u%%", value);
  painter.centered(160, 54, percentage.data(), FontSize::display, theme.text,
                   180);
  const std::uint16_t ink =
      snapshot.degraded ? theme.warning : theme.brand;
  const int pos = 24 + static_cast<int>(value) * 272 / 100;
  for (int index = 0; index <= 20; ++index) {
    const int x = 24 + index * 272 / 20;
    const bool major = (index % 5) == 0;
    const int height = major ? 18 : 10;
    fillRoundRect(painter.framebuffer, {x - 1, 148 - height, 2, height}, 1,
                  major ? theme.text : theme.muted);
  }
  fillRoundRect(painter.framebuffer, {24, 152, 272, 2}, 1, theme.surface);
  fillRoundRect(painter.framebuffer, {24, 151, pos - 24, 4}, 2, ink);
  fillRoundRect(painter.framebuffer, {pos - 1, 112, 3, 48}, 1, ink);
  fillRoundRect(painter.framebuffer, {8, 190, 88, 44}, 14, theme.raised);
  fillRoundRect(painter.framebuffer, {104, 190, 112, 44}, 14, theme.surface);
  fillRoundRect(painter.framebuffer, {224, 190, 88, 44}, 14, theme.brand);
  painter.centered(52, 199, "-", FontSize::title, theme.text, 56);
  painter.centered(160, 201, translated(snapshot, "Gotowe", "Done"),
                   FontSize::body, theme.text, 88);
  painter.centered(268, 199, "+", FontSize::title, theme.on_brand, 56);
}

void drawScreensaver(Painter& painter, const CompactSnapshot& snapshot,
                     const ThemeTokens& theme, const StationTheme& station) {
  fill(painter.framebuffer, theme.canvas);
  if (snapshot.screen == ScreenKind::screensaver_cat) {
    drawCat(painter, snapshot, theme, station);
    return;
  }
  painter.text(kHomeMargin, 10, station.name, FontSize::title, theme.text, 230);
  if (snapshot.screen == ScreenKind::screensaver_bars) {
    constexpr std::array<int, 12> base{{32, 52, 78, 96, 68, 44, 84, 112, 92, 60, 38, 72}};
    for (std::size_t index = 0; index < base.size(); ++index) {
      const int phase = (snapshot.animation_frame + index) % 6;
      const int height = std::max(18, base[index] - phase * 5);
      fillRoundRect(painter.framebuffer,
                    {34 + static_cast<int>(index) * 22, 182 - height, 12,
                     height},
                    6, index % 3 == 0 ? station.accent_alt : station.accent);
    }
  } else if (snapshot.screen == ScreenKind::screensaver_orbit) {
    const int pulse = snapshot.animation_frame % 6;
    fillCircle(painter.framebuffer, 160, 120, 74 + pulse, theme.raised);
    fillCircle(painter.framebuffer, 160, 120, 58, theme.canvas);
    fillCircle(painter.framebuffer, 160, 120, 28, station.accent);
    painter.centered(160, 108, station.short_name, FontSize::body,
                     station.on_accent, 48);
  } else {
    const int pulse = snapshot.animation_frame % 8;
    fillCircle(painter.framebuffer, 160, 120, 72 + pulse, theme.raised);
    fillCircle(painter.framebuffer, 160, 120, 50 + pulse / 2, theme.canvas);
    fillCircle(painter.framebuffer, 160, 120, 28, station.accent);
    painter.centered(160, 108, station.short_name, FontSize::body,
                     station.on_accent, 48);
  }
  painter.centered(160, 214, metadataTitle(snapshot), FontSize::caption,
                   theme.muted, 286);
}

void drawStateScreen(Painter& painter, const CompactSnapshot& snapshot,
                     const ThemeTokens& theme, const StationTheme& station,
                     const char* polish_title, const char* english_title,
                     const char* polish_detail, const char* english_detail,
                     std::uint16_t, const char* polish_action,
                     const char* english_action) {
  fill(painter.framebuffer, theme.canvas);
  drawUtilityHeader(painter, theme, "Open Radio");
  fillRoundRect(painter.framebuffer, {16, 54, 288, 116}, 18, theme.surface);
  painter.text(32, 66, translated(snapshot, polish_title, english_title),
               FontSize::title, theme.text, 252);
  painter.text(32, 94, station.name, FontSize::caption, station.accent, 252);
  drawWrappedText(painter, 32, 116,
                  translated(snapshot, polish_detail, english_detail),
                  FontSize::body, theme.text, 252, 2);
  fillRoundRect(painter.framebuffer, {8, 190, 304, 44}, 14, theme.brand);
  painter.centered(160, 201, translated(snapshot, polish_action, english_action),
                   FontSize::body, theme.on_brand, 270);
}

void drawWifiStatus(Painter& painter, const CompactSnapshot& snapshot,
                    const ThemeTokens& theme, const StationTheme& station) {
  fill(painter.framebuffer, theme.canvas);
  if (snapshot.console_active) {
    // Console session: the QR is the interface — the firmware overlays the
    // real code (http://<address>/) on the white canvas, mirroring the
    // provisioning QR geometry, so any phone camera lands on the picker
    // without typing anything.
    painter.text(8, 8, translated(snapshot, "Konsola", "Console"),
                 FontSize::caption, theme.muted, 112);
    painter.text(8, 30, translated(snapshot, "Stacje", "Stations"),
                 FontSize::body, theme.text, 112);
    painter.text(8, 56,
                 snapshot.device_address != nullptr ? snapshot.device_address
                                                    : "",
                 FontSize::caption, theme.brand, 112);
    painter.text(8, 78, translated(snapshot, "1. Skanuj QR", "1. Scan QR"),
                 FontSize::caption, theme.text, 112);
    painter.text(8, 100,
                 translated(snapshot, "2. Wybierz stacje", "2. Pick stations"),
                 FontSize::caption, theme.text, 112);
    painter.text(8, 122, translated(snapshot, "3. Zapisz", "3. Save"),
                 FontSize::caption, theme.text, 112);
    painter.text(8, 152,
                 translated(snapshot, "Radio wróci samo", "Radio resumes"),
                 FontSize::caption, theme.muted, 112);
    fillRect(painter.framebuffer, {128, 4, 184, 184}, rgb565(255, 255, 255));
    painter.centered(220, 84, "QR", FontSize::title, rgb565(0, 0, 0), 120);
    fillRoundRect(painter.framebuffer, {8, 190, 304, 44}, 14, theme.brand);
    painter.centered(160, 201,
                     translated(snapshot, "Zamknij konsolę", "Close console"),
                     FontSize::body, theme.on_brand, 270);
    return;
  }
  drawUtilityHeader(painter, theme, "Wi-Fi");
  fillRoundRect(painter.framebuffer, {16, 54, 288, 116}, 18, theme.surface);
  painter.text(32, 66,
               translated(snapshot,
                          snapshot.wifi_online ? "Wi-Fi połączone"
                                               : "Brak Wi-Fi",
                          snapshot.wifi_online ? "Wi-Fi connected"
                                               : "Wi-Fi unavailable"),
               FontSize::title, theme.text, 252);
  const bool has_address = snapshot.wifi_online &&
                           snapshot.device_address != nullptr &&
                           snapshot.device_address[0] != '\0';
  if (has_address) {
    // The address is the way into the device console: mDNS name first, the
    // raw IP as the fallback for networks without mDNS.
    painter.text(32, 98, "http://open-radio.local", FontSize::body,
                 station.accent, 252);
    std::array<char, 48> address{};
    std::snprintf(address.data(), address.size(), "IP: %s",
                  snapshot.device_address);
    painter.text(32, 124, address.data(), FontSize::body, theme.text, 252);
    painter.text(32, 148,
                 translated(snapshot, "Otwórz w przeglądarce w tej samej sieci.",
                            "Open in a browser on the same network."),
                 FontSize::caption, theme.muted, 252);
  } else {
    painter.text(32, 98, station.name, FontSize::caption, station.accent, 252);
    drawWrappedText(painter, 32, 122,
                    translated(snapshot,
                               snapshot.wifi_online
                                   ? "Znana sieć działa. Możesz lokalnie dodać kolejną."
                                   : "Brak połączenia. Otwórz lokalną konfigurację.",
                               snapshot.wifi_online
                                   ? "The known network works. You can add another locally."
                                   : "No connection. Open local setup."),
                    FontSize::body, theme.text, 252, 2);
  }
  fillRoundRect(painter.framebuffer, {8, 190, 304, 44}, 14, theme.brand);
  painter.centered(160, 201,
                   translated(snapshot, "Dodaj sieć Wi-Fi", "Add Wi-Fi network"),
                   FontSize::body, theme.on_brand, 270);
}

void drawBluetoothPairing(Painter& painter, const CompactSnapshot& snapshot,
                          const ThemeTokens& theme) {
  fill(painter.framebuffer, theme.canvas);
  drawUtilityHeader(painter, theme, "Bluetooth");
  fillRoundRect(painter.framebuffer, {8, 56, 304, 112}, 18, theme.surface);
  drawIconMask24(painter.framebuffer, 24, 78, generated::kIconBluetooth,
                 theme.brand);
  const char* title = translated(snapshot, "Gotowy do skanowania", "Ready to scan");
  const char* detail = translated(snapshot, "Wyszukaj głośnik A2DP z kodekiem SBC.",
                                  "Find an A2DP speaker using SBC.");
  const char* action = translated(snapshot, "Skanuj głośniki", "Scan speakers");
  std::uint16_t actionColor = theme.brand;
  if (snapshot.bluetooth_state == BluetoothState::scanning) {
    title = translated(snapshot, "Szukam głośników…", "Scanning for speakers…");
    detail = translated(snapshot, "Skan Bluetooth Classic A2DP jest aktywny.",
                        "Bluetooth Classic A2DP scan is active.");
    action = translated(snapshot, "Skanowanie…", "Scanning…");
    actionColor = theme.raised;
  } else if (snapshot.bluetooth_state == BluetoothState::found) {
    title = snapshot.bluetooth_candidate_name != nullptr &&
                    snapshot.bluetooth_candidate_name[0] != '\0'
                ? snapshot.bluetooth_candidate_name
                : translated(snapshot, "Znaleziono głośnik", "Speaker found");
    detail = translated(snapshot, "A2DP · SBC · znaleziony", "A2DP · SBC · found");
    action = translated(snapshot, "Łączę automatycznie…", "Connecting automatically…");
    actionColor = theme.raised;
  } else if (snapshot.bluetooth_state == BluetoothState::connecting) {
    title = snapshot.bluetooth_candidate_name != nullptr &&
                    snapshot.bluetooth_candidate_name[0] != '\0'
                ? snapshot.bluetooth_candidate_name
                : translated(snapshot, "Łączenie", "Connecting");
    detail = translated(snapshot, "Łączę głośnik przez A2DP.",
                        "Connecting the speaker over A2DP.");
    action = translated(snapshot, "Łączenie…", "Connecting…");
    actionColor = theme.raised;
  } else if (snapshot.bluetooth_state == BluetoothState::connected) {
    title = snapshot.bluetooth_candidate_name != nullptr &&
                    snapshot.bluetooth_candidate_name[0] != '\0'
                ? snapshot.bluetooth_candidate_name
                : translated(snapshot, "Głośnik połączony", "Speaker connected");
    detail = translated(snapshot,
                        snapshot.bluetooth_remembered
                            ? "A2DP · SBC · zapamiętany"
                            : "A2DP · SBC · połączony",
                        snapshot.bluetooth_remembered
                            ? "A2DP · SBC · remembered"
                            : "A2DP · SBC · connected");
    action = translated(snapshot, "Skanuj ponownie", "Scan again");
  } else if (snapshot.bluetooth_state == BluetoothState::error) {
    title = translated(snapshot, "Nie znaleziono głośnika", "No speaker found");
    detail = translated(snapshot, "Głośnik radia nadal działa. Możesz ponowić skan.",
                        "The radio speaker still works. You can retry the scan.");
    action = translated(snapshot, "Spróbuj ponownie", "Try again");
  }
  painter.text(64, 70, title, FontSize::title, theme.text, 240);
  // 226 was narrower than the card allows and cut one of the state sentences.
  // The card runs to x=312 and the text starts at 64, so 240 is what is
  // actually there.
  drawWrappedText(painter, 64, 104, detail, FontSize::body, theme.muted, 240, 3);
  if (snapshot.bluetooth_remembered &&
      snapshot.bluetooth_state == BluetoothState::connected) {
    painter.text(64, 146, translated(snapshot, "ZAPAMIĘTANY", "REMEMBERED"),
                 FontSize::caption, theme.success, 120);
  }
  fillRoundRect(painter.framebuffer, {8, 190, 304, 44}, 14, actionColor);
  painter.centered(160, 201, action, FontSize::body,
                   actionColor == theme.brand ? theme.on_brand : theme.muted,
                   270);
}

void drawOnboarding(Painter& painter, const CompactSnapshot& snapshot,
                    const ThemeTokens& theme,
                    const char* step, const char* polish_title,
                    const char* english_title, const char* polish_detail,
                    const char* english_detail, const char* polish_secondary,
                    const char* english_secondary, const char* polish_primary,
                    const char* english_primary) {
  fill(painter.framebuffer, theme.canvas);
  painter.text(kHomeMargin, 10, "Open Radio", FontSize::body, theme.muted, 180);
  painter.text(276, 10, step, FontSize::caption, theme.muted, 36);
  fillRoundRect(painter.framebuffer, {kHomeMargin, 58, 88, 92}, 24, theme.surface);
  drawIconMask24(painter.framebuffer, 44, 92, generated::kIconCheck,
                 theme.brand);
  painter.text(116, 62, translated(snapshot, polish_title, english_title),
               FontSize::title, theme.text, 190);
  drawWrappedText(painter, 116, 92,
                  translated(snapshot, polish_detail, english_detail),
                  FontSize::body, theme.muted, 186, 2);
  if (snapshot.setup_access_code != nullptr &&
      snapshot.setup_access_code[0] != '\0') {
    fillRoundRect(painter.framebuffer, {116, 136, 186, 36}, 12, theme.raised);
    painter.text(126, 145,
                 translated(snapshot, "Kod Wi-Fi", "Wi-Fi code"),
                 FontSize::caption, theme.muted, 62);
    painter.text(190, 145, snapshot.setup_access_code, FontSize::caption,
                 theme.text, 102);
  }
  fillRoundRect(painter.framebuffer, {8, 190, 96, 44}, 14, theme.raised);
  painter.centered(56, 202,
                   translated(snapshot, polish_secondary, english_secondary),
                   FontSize::caption, theme.text, 78);
  fillRoundRect(painter.framebuffer, {112, 190, 200, 44}, 14, theme.brand);
  painter.centered(212, 201,
                   translated(snapshot, polish_primary, english_primary),
                   FontSize::body, theme.on_brand, 166);
}

void drawWifiQrScreen(Painter& painter, const CompactSnapshot& snapshot,
                      const ThemeTokens& theme) {
  const bool onboarding = snapshot.screen == ScreenKind::onboarding_wifi;
  fill(painter.framebuffer, theme.canvas);
  painter.text(8, 8, onboarding ? "1/3" : "Wi-Fi", FontSize::caption,
               theme.muted, 112);
  painter.text(8, 30,
               onboarding
                   ? translated(snapshot, "Połącz telefon", "Connect phone")
                   : translated(snapshot, "Dodaj sieć", "Add network"),
               FontSize::body, theme.text, 112);
  painter.text(8, 52, "OpenRadio-Setup", FontSize::caption, theme.brand, 112);
  painter.text(8, 68,
               translated(snapshot, "1. Skanuj QR", "1. Scan QR"),
               FontSize::caption, theme.text, 112);
  painter.text(8, 90,
               translated(snapshot, "2. Połącz", "2. Join"),
               FontSize::caption, theme.text, 112);
  painter.text(8, 112,
               translated(snapshot, "3. Wybierz Wi-Fi", "3. Choose Wi-Fi"),
               FontSize::caption, theme.text, 112);
  painter.text(8, 144,
               translated(snapshot, "Kod awaryjny", "Backup code"),
               FontSize::caption, theme.muted, 112);
  drawWrappedText(
      painter, 8, 162,
      snapshot.setup_access_code != nullptr ? snapshot.setup_access_code : "",
      FontSize::caption, theme.text, 112, 2);
  fillRect(painter.framebuffer, {128, 4, 184, 184}, rgb565(255, 255, 255));
  painter.centered(220, 84, "QR", FontSize::title, rgb565(0, 0, 0), 120);
  if (onboarding) {
    fillRoundRect(painter.framebuffer, {8, 190, 96, 44}, 14, theme.raised);
    painter.centered(56, 202, translated(snapshot, "Język", "Language"),
                     FontSize::caption, theme.text, 78);
    const std::uint16_t continueColor =
        snapshot.wifi_online ? theme.brand : theme.raised;
    fillRoundRect(painter.framebuffer, {112, 190, 200, 44}, 14, continueColor);
    painter.centered(
        212, 201,
        snapshot.wifi_online
            ? translated(snapshot, "Dalej", "Continue")
            : translated(snapshot, "Czekaj", "Wait"),
        FontSize::body,
        snapshot.wifi_online ? theme.on_brand : theme.muted, 166);
    return;
  }
  fillRoundRect(painter.framebuffer, {8, 190, 148, 44}, 14, theme.brand);
  fillRoundRect(painter.framebuffer, {164, 190, 148, 44}, 14, theme.raised);
  painter.centered(82, 201, translated(snapshot, "Nowy kod QR", "New QR code"),
                   FontSize::caption, theme.on_brand, 122);
  painter.centered(238, 201,
                   snapshot.wifi_online
                       ? translated(snapshot, "Zamknij", "Close")
                       : translated(snapshot, "Czekam", "Waiting"),
                   FontSize::caption,
                   snapshot.wifi_online ? theme.text : theme.muted, 122);
}

RenderResult invalidResult(RenderStatus status) { return {status, 0, 0}; }

}  // namespace

bool isValid(FramebufferView framebuffer) {
  std::size_t required = 0;
  return requiredPixels(framebuffer, required);
}

bool fill(FramebufferView framebuffer, std::uint16_t color) {
  std::size_t required = 0;
  if (!requiredPixels(framebuffer, required)) return false;
  std::fill_n(framebuffer.pixels, required, color);
  return true;
}

bool fillRect(FramebufferView framebuffer, Rect rect, std::uint16_t color) {
  if (!isValid(framebuffer)) return false;
  if (rect.width <= 0 || rect.height <= 0) return true;
  const auto raw_right = static_cast<std::int64_t>(rect.x) + rect.width;
  const auto raw_bottom = static_cast<std::int64_t>(rect.y) + rect.height;
  const int left = std::max(0, rect.x);
  const int top = std::max(0, rect.y);
  const int right = static_cast<int>(
      std::min<std::int64_t>(framebuffer.width, raw_right));
  const int bottom = static_cast<int>(
      std::min<std::int64_t>(framebuffer.height, raw_bottom));
  if (left >= right || top >= bottom) return true;
  for (int y = top; y < bottom; ++y) {
    auto* row = framebuffer.pixels + static_cast<std::size_t>(y) *
                                         static_cast<std::size_t>(framebuffer.width);
    std::fill(row + left, row + right, color);
  }
  return true;
}

bool fillRoundRect(FramebufferView framebuffer, Rect rect, int radius,
                   std::uint16_t color) {
  if (!isValid(framebuffer) || radius < 0) return false;
  if (rect.width <= 0 || rect.height <= 0) return true;
  radius = std::min(radius, std::min(rect.width, rect.height) / 2);
  if (radius == 0) return fillRect(framebuffer, rect, color);
  const int radius_squared = radius * radius;
  for (int y = std::max(0, rect.y);
       y < std::min(framebuffer.height, rect.y + rect.height); ++y) {
    for (int x = std::max(0, rect.x);
         x < std::min(framebuffer.width, rect.x + rect.width); ++x) {
      const int local_x = x - rect.x;
      const int local_y = y - rect.y;
      const int corner_x = local_x < radius
                               ? radius - 1 - local_x
                               : local_x >= rect.width - radius
                                     ? local_x - (rect.width - radius)
                                     : 0;
      const int corner_y = local_y < radius
                               ? radius - 1 - local_y
                               : local_y >= rect.height - radius
                                     ? local_y - (rect.height - radius)
                                     : 0;
      if (corner_x == 0 || corner_y == 0 ||
          corner_x * corner_x + corner_y * corner_y < radius_squared) {
        putPixel(framebuffer, x, y, color);
      }
    }
  }
  return true;
}

bool drawIconMask24(FramebufferView framebuffer, int x, int y,
                    const IconMask24& mask, std::uint16_t color) {
  if (!isValid(framebuffer)) return false;
  for (int row = 0; row < 24; ++row) {
    const auto bits = mask.rows[static_cast<std::size_t>(row)];
    for (int column = 0; column < 24; ++column) {
      if ((bits & (1U << static_cast<unsigned>(column))) != 0U) {
        putPixel(framebuffer, x + column, y + row, color);
      }
    }
  }
  return true;
}

int measureText(const char* value, FontSize size) {
  if (value == nullptr) return 0;
  const auto& face = generated::interFace(size);
  const auto* cursor = reinterpret_cast<const unsigned char*>(value);
  int width = 0;
  while (*cursor != 0U) width += glyphFor(face, decodeUtf8(cursor)).advance;
  return width;
}

TextRenderResult drawText(FramebufferView framebuffer, int x, int y,
                          const char* value, FontSize size,
                          std::uint16_t color, int maximum_width,
                          bool ellipsize) {
  if (!isValid(framebuffer) || value == nullptr || maximum_width < 0) {
    return {false, false, 0};
  }
  const auto& face = generated::interFace(size);
  const int full_width = measureText(value, size);
  const bool truncated = full_width > maximum_width;
  const auto& ellipsis = glyphFor(face, 0x2026U);
  const int content_limit = truncated && ellipsize
                                ? std::max(0, maximum_width - ellipsis.advance)
                                : maximum_width;
  const auto* cursor = reinterpret_cast<const unsigned char*>(value);
  int drawn_width = 0;
  while (*cursor != 0U) {
    const auto codepoint = decodeUtf8(cursor);
    const auto& glyph = glyphFor(face, codepoint);
    if (drawn_width + glyph.advance > content_limit) break;
    drawGlyph(framebuffer, x + drawn_width, y, face, glyph, color);
    drawn_width += glyph.advance;
  }
  if (truncated && ellipsize && drawn_width + ellipsis.advance <= maximum_width) {
    drawGlyph(framebuffer, x + drawn_width, y, face, ellipsis, color);
    drawn_width += ellipsis.advance;
  }
  return {true, truncated, drawn_width};
}

std::uint64_t hashFramebuffer(FramebufferView framebuffer) {
  std::size_t required = 0;
  if (!requiredPixels(framebuffer, required)) return 0;
  std::uint64_t hash = kFnvOffset;
  for (std::size_t index = 0; index < required; ++index) {
    const auto pixel = framebuffer.pixels[index];
    hash ^= static_cast<std::uint8_t>(pixel >> 8U);
    hash *= kFnvPrime;
    hash ^= static_cast<std::uint8_t>(pixel & 0xffU);
    hash *= kFnvPrime;
  }
  return hash;
}

RenderResult renderNowPlaying(FramebufferView framebuffer,
                              const CompactSnapshot& snapshot,
                              const StationTheme* stations,
                              std::size_t station_count) {
  if (!isValid(framebuffer)) return invalidResult(RenderStatus::invalid_buffer);
  if (framebuffer.width != generated::kWidth ||
      framebuffer.height != generated::kHeight) {
    return invalidResult(RenderStatus::invalid_dimensions);
  }
  if (stations == nullptr || snapshot.station_index >= station_count ||
      snapshot.requested_station_index >= station_count) {
    return invalidResult(RenderStatus::invalid_station);
  }
  Painter painter{framebuffer};
  const auto& station = stations[snapshot.station_index];
  const auto& theme = generated::themeFor(snapshot.theme);
  drawNowPlaying(painter, snapshot, theme, station);
  return {RenderStatus::ok, hashFramebuffer(framebuffer), painter.truncated};
}

void setRuntimeStationLogos(const RuntimeStationLogo* logos, std::size_t count) {
  runtime_logos = logos;
  runtime_logo_count = logos == nullptr ? 0 : count;
}

RenderResult renderScreen(FramebufferView framebuffer,
                          const CompactSnapshot& snapshot,
                          const StationTheme* stations,
                          std::size_t station_count) {
  if (snapshot.screen == ScreenKind::now_playing_editorial ||
      snapshot.screen == ScreenKind::now_playing_glance) {
    auto home = snapshot;
    home.variant = snapshot.screen == ScreenKind::now_playing_glance
                       ? HomeVariant::glance
                       : HomeVariant::editorial;
    return renderNowPlaying(framebuffer, home, stations, station_count);
  }
  if (!isValid(framebuffer)) return invalidResult(RenderStatus::invalid_buffer);
  if (framebuffer.width != generated::kWidth ||
      framebuffer.height != generated::kHeight) {
    return invalidResult(RenderStatus::invalid_dimensions);
  }
  if (stations == nullptr || snapshot.station_index >= station_count) {
    return invalidResult(RenderStatus::invalid_station);
  }
  Painter painter{framebuffer};
  const auto& station = stations[snapshot.station_index];
  const auto& theme = generated::themeFor(snapshot.theme);

  if (snapshot.screen == ScreenKind::display_off) {
    fill(framebuffer, 0x0000);
  } else if (snapshot.screen == ScreenKind::screensaver_pulse ||
             snapshot.screen == ScreenKind::screensaver_bars ||
             snapshot.screen == ScreenKind::screensaver_orbit ||
             snapshot.screen == ScreenKind::screensaver_cat) {
    drawScreensaver(painter, snapshot, theme, station);
  } else if (snapshot.screen == ScreenKind::stations) {
    drawStations(painter, snapshot, theme, stations, station_count);
  } else if (snapshot.screen == ScreenKind::volume_control) {
    drawPercentControl(painter, snapshot, theme,
                       translated(snapshot, "Głośność", "Volume"),
                       snapshot.volume);
  } else if (snapshot.screen == ScreenKind::brightness_control) {
    drawPercentControl(painter, snapshot, theme,
                       translated(snapshot, "Jasność", "Brightness"),
                       snapshot.brightness);
  } else if (snapshot.screen == ScreenKind::settings_quick ||
             snapshot.screen == ScreenKind::settings_system ||
             snapshot.screen == ScreenKind::settings_display) {
    drawSettings(painter, snapshot, theme);
  } else if (snapshot.screen == ScreenKind::now_playing_air) {
    drawNowPlayingAir(painter, snapshot, theme, stations, station_count);
  // UNREACHABLE ON THE DEVICE — MARKED FOR REMOVAL IN 0.5. No firmware path
  // sets this screen (see ui_controller.hpp, Screen::Sensors). It draws a raw
  // gas resistance in kOhm, which is precisely the figure the air homes avoid
  // showing as a number; if anyone ever routes to it, that tile needs a
  // qualifier first.
  } else if (snapshot.screen == ScreenKind::sensors) {
    drawSensors(painter, snapshot, theme);
  } else if (snapshot.screen == ScreenKind::noise) {
    drawNoise(painter, snapshot, theme);
  } else if (snapshot.screen == ScreenKind::wifi_status) {
    if (snapshot.wifi_portal_active) {
      drawWifiQrScreen(painter, snapshot, theme);
    } else {
      drawWifiStatus(painter, snapshot, theme, station);
    }
  } else if (snapshot.screen == ScreenKind::wifi_recovery) {
    drawStateScreen(painter, snapshot, theme, station, "Brak Wi-Fi",
                    "Wi-Fi unavailable", "Wracam do znanej sieci. Radio zrobi to automatycznie.",
                    "Returning to a known network automatically.", theme.warning,
                    "Spróbuj teraz", "Try now");
  } else if (snapshot.screen == ScreenKind::bluetooth_fallback) {
    drawStateScreen(painter, snapshot, theme, station, "Brak Bluetooth",
                    "Bluetooth lost", "Dźwięk automatycznie wrócił na głośnik radia.",
                    "Audio automatically returned to the radio speaker.",
                    theme.success, "Rozumiem", "OK");
  } else if (snapshot.screen == ScreenKind::unsupported) {
    drawStateScreen(painter, snapshot, theme, station, "Format niewspierany",
                    "Format not supported", "Ta stacja wymaga HLS lub HE-AAC.",
                    "This station requires HLS or HE-AAC.", theme.warning,
                    "Wybierz stację MP3", "Choose an MP3 station");
  } else if (snapshot.screen == ScreenKind::safe_mode) {
    drawStateScreen(painter, snapshot, theme, station, "Tryb bezpieczny",
                    "Safe mode", "Konfiguracja wymaga lokalnej naprawy.",
                    "Configuration requires local repair.", theme.danger,
                    "Diagnostyka", "Diagnostics");
  } else if (snapshot.screen == ScreenKind::onboarding_wifi) {
    drawWifiQrScreen(painter, snapshot, theme);
  } else if (snapshot.screen == ScreenKind::onboarding_first_sound) {
    drawOnboarding(painter, snapshot, theme, "2/3", "Radio już gra",
                   "Radio is playing", "Najpierw sprawdzamy lokalny głośnik.",
                   "The local speaker is checked first.", "Wstecz", "Back",
                   "Dalej", "Continue");
  } else if (snapshot.screen == ScreenKind::onboarding_bluetooth) {
    // "Bluetooth opcjonalny" did not fit and shipped as "Bluetooth opcjon…".
    // The optionality is already the whole point of the sentence underneath, so
    // the title says the noun and the subtitle keeps the meaning.
    drawOnboarding(painter, snapshot, theme, "3/3", "Bluetooth",
                   "Bluetooth", "Możesz zostać przy głośniku radia.",
                   "You can keep using the radio speaker.", "Bez BT", "No BT",
                   "Paruj", "Pair");
  } else if (snapshot.screen == ScreenKind::bluetooth_pairing) {
    drawBluetoothPairing(painter, snapshot, theme);
  } else if (snapshot.screen == ScreenKind::diagnostics) {
    fill(framebuffer, theme.canvas);
    drawUtilityHeader(painter, theme,
                      translated(snapshot, "Diagnostyka", "Diagnostics"));
    constexpr std::array<const char*, 3> polish{{"Konfiguracja", "Katalog stacji",
                                                 "Wi-Fi i Bluetooth"}};
    constexpr std::array<const char*, 3> english{{"Configuration", "Station catalog",
                                                  "Wi-Fi and Bluetooth"}};
    for (std::size_t index = 0; index < polish.size(); ++index) {
      const int top = 54 + static_cast<int>(index) * 42;
      fillRoundRect(framebuffer, {8, top, 304, 38}, 11, theme.surface);
      painter.text(20, top + 10, translated(snapshot, polish[index], english[index]),
                   FontSize::body, theme.text, 210);
      painter.text(260, top + 11, "PASS", FontSize::caption, theme.success, 42);
    }
    fillRoundRect(framebuffer, {8, 190, 304, 44}, 14, theme.raised);
    painter.centered(160, 201, translated(snapshot, "Wstecz", "Back"),
                     FontSize::body, theme.text, 270);
  } else if (snapshot.screen == ScreenKind::about) {
    fill(framebuffer, theme.canvas);
    // Rebuilt around the one thing anybody opens an About screen for, which the
    // old one did not show at all: the VERSION. Before this it offered a blue
    // square reading "A2", a product name, and three status lines in three
    // different colours — a rainbow of unrelated facts with the fact missing.
    drawUtilityHeader(painter, theme, translated(snapshot, "O radiu", "About"));

    fillRoundRect(framebuffer, {8, 60, 304, 96}, 18, theme.surface);
    drawSignalCube(painter.framebuffer, 24, 76, 64, theme.success, theme.surface,
                   theme.warning);
    painter.text(104, 68, "Open Radio", FontSize::title, theme.text, 200);
    painter.text(104, 96, snapshot.firmware_version != nullptr
                              ? snapshot.firmware_version
                              : "dev",
                 FontSize::display, theme.text, 200);
    painter.text(104, 134, "Core2 · GPL-3.0", FontSize::caption, theme.muted, 200);

    // One line, one colour, and only the part that is true. Three coloured
    // claims side by side read as decoration; a single muted sentence reads as
    // a statement.
    // One pixel in: Inter gives "f" a negative left bearing, so a caption set at
    // the margin puts ink at x=7 and the geometry audit rejects it. The same
    // class of problem as the icon masks, on the other side of the type.
    // The author's name and his own domain come FIRST, and they are the pair
    // that has to outlive everything else here. A cube can sit on a shelf for
    // years; open-radio.me is a project domain with a renewal date, and the
    // day it lapses this screen would point a curious owner at nothing. A name
    // does not expire, and fiedoruk.pl is the last address that would ever be
    // let go — so provenance is anchored to that, and the project site is
    // listed as what it is: where the project currently lives.
    painter.text(9, 166, "Tomasz Fiedoruk · fiedoruk.pl", FontSize::caption,
                 theme.text, 302);
    painter.text(9, 184,
                 translated(snapshot, "open-radio.me · bez telemetrii i konta",
                            "open-radio.me · no telemetry, no account"),
                 FontSize::caption, theme.muted, 302);

    fillRoundRect(framebuffer, {8, 206, 304, 26}, 12, theme.raised);
    painter.centered(160, 211, translated(snapshot, "Diagnostyka", "Diagnostics"),
                     FontSize::body, theme.text, 270);
  } else if (snapshot.screen == ScreenKind::market) {
    fill(framebuffer, theme.canvas);
    drawUtilityHeader(painter, theme,
                      translated(snapshot, "Region i stacje", "Region and stations"));
    constexpr std::array<const char*, 4> polish{{"Region", "Kraj", "Stacje", "Języki"}};
    constexpr std::array<const char*, 4> english{{"Region", "Country", "Stations", "Languages"}};
    constexpr std::array<const char*, 4> values{{"Cała Polska", "PL", "", "PL · EN"}};
    for (std::size_t index = 0; index < polish.size(); ++index) {
      const int top = 52 + static_cast<int>(index) * 31;
      painter.text(kHomeMargin, top, translated(snapshot, polish[index], english[index]),
                   FontSize::body, theme.text, 136);
      // The stations row said "9", which is the number of slots and reads as the
      // number of stations. The cube browses a directory of 236 and the box says
      // so; a screen that answers "9" to the same question contradicts it on the
      // one surface a buyer cannot check against anything. Both numbers, then.
      // The 236 is pinned to release/claims.json by check-claims.
      const char* value = index == 2 ? translated(snapshot, "9 z 236", "9 of 236")
                                     : values[index];
      painter.text(kHomeColumnTwo, top, value, FontSize::body,
                   index == 0 ? theme.brand : theme.muted, 110);
    }
    fillRoundRect(framebuffer, {8, 190, 304, 44}, 14, theme.raised);
    painter.centered(160, 201, translated(snapshot, "Wbudowane lokalnie", "Embedded locally"),
                     FontSize::body, theme.text, 270);
  }
  return {RenderStatus::ok, hashFramebuffer(framebuffer), painter.truncated};
}

}  // namespace open_radio::ui
